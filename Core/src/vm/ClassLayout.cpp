#include "phoneme/vm/ClassLayout.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace phoneme::vm
{
  namespace
  {

    constexpr u16 kAccStatic = 0x0008U;
    constexpr u16 kAccInterface = 0x0200U;
    constexpr u16 kAccAbstract = 0x0400U;

    [[nodiscard]] std::string normalize_name(std::string_view name)
    {
      std::string normalized(name);
      std::replace(normalized.begin(), normalized.end(), '.', '/');
      return normalized;
    }

    [[nodiscard]] std::string field_resolution_key(
        std::string_view owner,
        std::string_view name,
        std::string_view descriptor,
        bool require_static)
    {
      std::string key;
      key.reserve(owner.size() + name.size() + descriptor.size() + 4U);
      key.append(owner);
      key.push_back('\n');
      key.append(name);
      key.push_back('\n');
      key.append(descriptor);
      key.push_back(require_static ? '\1' : '\0');
      return key;
    }

  } // namespace

  Result<std::shared_ptr<const ClassLayout>> ClassStateRegistry::layout(
      std::string_view class_name)
  {
    const std::string normalized = normalize_name(class_name);
    if (normalized.empty())
    {
      return fail(ErrorCode::invalid_argument,
                  "class layout name must not be empty");
    }

    {
      std::scoped_lock lock(mutex_);
      if (const auto iterator = layouts_.find(normalized);
          iterator != layouts_.end())
      {
        return iterator->second;
      }
    }

    auto built = build_layout(normalized);
    if (!built)
    {
      return std::unexpected(built.error());
    }

    std::scoped_lock lock(mutex_);
    const auto [iterator, inserted] = layouts_.emplace(normalized, *built);
    (void)inserted;
    return iterator->second;
  }

  Result<FieldLocation> ClassStateRegistry::resolve_field(
      std::string_view owner,
      std::string_view name,
      std::string_view descriptor,
      bool require_static)
  {
    if (owner.empty() || name.empty() || descriptor.empty())
    {
      return fail(ErrorCode::invalid_argument,
                  "field reference is incomplete");
    }

    std::string current = normalize_name(owner);
    const std::string cache_key = field_resolution_key(
        current, name, descriptor, require_static);
    {
      std::scoped_lock lock(mutex_);
      if (const auto cached = resolved_fields_.find(cache_key);
          cached != resolved_fields_.end())
      {
        return cached->second;
      }
    }
    const auto cache_location = [this, &cache_key](FieldLocation location) {
      std::scoped_lock lock(mutex_);
      const auto [iterator, inserted] = resolved_fields_.emplace(
          cache_key, std::move(location));
      (void)inserted;
      return iterator->second;
    };

    usize hierarchy_depth = 0;
    while (!current.empty())
    {
      if (++hierarchy_depth > 256)
      {
        return fail(ErrorCode::malformed_class,
                    "field hierarchy exceeds the supported depth");
      }

      auto loaded = classes_.load(current);
      if (!loaded)
      {
        return std::unexpected(loaded.error());
      }

      for (const classfile::Field &field : (*loaded)->fields())
      {
        if (field.name != name || field.descriptor != descriptor)
        {
          continue;
        }
        const bool is_static = (field.access_flags & kAccStatic) != 0;
        if (is_static != require_static)
        {
          return fail(ErrorCode::invalid_state,
                      "field staticness does not match the bytecode opcode");
        }

        if (is_static)
        {
          return cache_location(FieldLocation{
              .declaring_class = current,
              .name = std::string(name),
              .descriptor = std::string(descriptor),
              .index = 0,
              .is_static = true,
              .constant_value_index = field.constant_value_index,
          });
        }

        auto declaring_layout = layout(current);
        if (!declaring_layout)
        {
          return std::unexpected(declaring_layout.error());
        }
        const std::string key = field_key(current, name, descriptor);
        const auto offset = (*declaring_layout)->instance_fields.find(key);
        if (offset == (*declaring_layout)->instance_fields.end())
        {
          return fail(ErrorCode::internal_error,
                      "instance field is absent from its class layout");
        }
        return cache_location(FieldLocation{
            .declaring_class = current,
            .name = std::string(name),
            .descriptor = std::string(descriptor),
            .index = offset->second,
            .is_static = false,
            .constant_value_index = std::nullopt,
        });
      }
      current = (*loaded)->super_name();
    }

    return fail(ErrorCode::class_not_found,
                "field was not found in the class hierarchy: " +
                    normalize_name(owner) + "." + std::string(name) +
                    std::string(descriptor));
  }

  Result<ObjectRef> ClassStateRegistry::allocate_instance(
      Heap &heap,
      std::string_view class_name)
  {
    auto loaded = classes_.load(class_name);
    if (!loaded)
    {
      return std::unexpected(loaded.error());
    }
    if (((*loaded)->access_flags() & (kAccInterface | kAccAbstract)) != 0U)
    {
      return fail(ErrorCode::invalid_argument,
                  "cannot allocate an interface or abstract class");
    }
    auto class_layout = layout(class_name);
    if (!class_layout)
    {
      return std::unexpected(class_layout.error());
    }
    auto reference = heap.allocate_object((*class_layout)->class_name,
                                          (*class_layout)->instance_field_slots);
    if (!reference)
    {
      return std::unexpected(reference.error());
    }
    for (usize index = 0;
         index < (*class_layout)->instance_defaults.size();
         ++index)
    {
      auto initialized = heap.set_field(*reference,
                                        index,
                                        (*class_layout)->instance_defaults[index]);
      if (!initialized)
      {
        return std::unexpected(initialized.error());
      }
    }
    return reference;
  }

  Result<Value> ClassStateRegistry::static_field(const FieldLocation &field)
  {
    if (!field.is_static)
    {
      return fail(ErrorCode::invalid_argument,
                  "requested field is not static");
    }
    const std::string key = field_key(field.declaring_class,
                                      field.name,
                                      field.descriptor);

    {
      std::scoped_lock lock(mutex_);
      if (const auto iterator = static_fields_.find(key);
          iterator != static_fields_.end())
      {
        return iterator->second;
      }
    }

    auto initial = default_value(field.descriptor);
    if (!initial)
    {
      return std::unexpected(initial.error());
    }
    if (field.constant_value_index.has_value())
    {
      auto owner = classes_.load(field.declaring_class);
      if (!owner)
        return std::unexpected(owner.error());
      auto constant = (*owner)->constant(*field.constant_value_index);
      if (!constant)
        return std::unexpected(constant.error());
      switch ((*constant)->kind)
      {
      case classfile::ConstantKind::integer:
        initial = Value::from_int(static_cast<i32>(
            static_cast<u32>((*constant)->bits)));
        break;
      case classfile::ConstantKind::float32:
        initial = Value::from_float(std::bit_cast<float>(
            static_cast<u32>((*constant)->bits)));
        break;
      case classfile::ConstantKind::long64:
        initial = Value::from_long(static_cast<i64>((*constant)->bits));
        break;
      case classfile::ConstantKind::float64:
        initial = Value::from_double(
            std::bit_cast<double>((*constant)->bits));
        break;
      case classfile::ConstantKind::string_ref:
        break;
      default:
        return fail(ErrorCode::malformed_class,
                    "field ConstantValue has an unsupported constant kind");
      }
    }

    std::scoped_lock lock(mutex_);
    const auto [iterator, inserted] = static_fields_.emplace(key, *initial);
    (void)inserted;
    return iterator->second;
  }

  Status ClassStateRegistry::set_static_field(const FieldLocation &field,
                                              Value value)
  {
    if (!field.is_static)
    {
      return fail(ErrorCode::invalid_argument,
                  "requested field is not static");
    }
    auto descriptor = parse_field_descriptor(field.descriptor);
    if (!descriptor)
    {
      return std::unexpected(descriptor.error());
    }

    const bool compatible = [&]() noexcept
    {
      switch (descriptor->kind)
      {
      case JavaTypeKind::boolean:
      case JavaTypeKind::byte:
      case JavaTypeKind::character:
      case JavaTypeKind::short_integer:
      case JavaTypeKind::integer:
        return value.kind() == ValueKind::int32;
      case JavaTypeKind::float32:
        return value.kind() == ValueKind::float32;
      case JavaTypeKind::long_integer:
        return value.kind() == ValueKind::int64;
      case JavaTypeKind::float64:
        return value.kind() == ValueKind::float64;
      case JavaTypeKind::reference:
      case JavaTypeKind::array:
        return value.kind() == ValueKind::reference;
      case JavaTypeKind::void_type:
        return false;
      }
      return false;
    }();

    if (!compatible)
    {
      return fail(ErrorCode::invalid_argument,
                  "static field value does not match its descriptor");
    }

    const std::string key = field_key(field.declaring_class,
                                      field.name,
                                      field.descriptor);
    std::scoped_lock lock(mutex_);
    static_fields_.insert_or_assign(key, value);
    return {};
  }

  void ClassStateRegistry::append_reference_roots(
      std::vector<ObjectRef> &roots) const
  {
    std::scoped_lock lock(mutex_);
    for (const auto &[key, value] : static_fields_)
    {
      (void)key;
      if (value.kind() != ValueKind::reference)
        continue;
      auto reference = value.as_reference();
      if (reference && !reference->is_null())
      {
        roots.push_back(*reference);
      }
    }
  }

  void ClassStateRegistry::clear() noexcept
  {
    std::scoped_lock lock(mutex_);
    layouts_.clear();
    resolved_fields_.clear();
    static_fields_.clear();
  }

  Result<std::shared_ptr<const ClassLayout>> ClassStateRegistry::build_layout(
      std::string class_name)
  {
    auto loaded = classes_.load(class_name);
    if (!loaded)
    {
      return std::unexpected(loaded.error());
    }

    ClassLayout result{
        .class_name = class_name,
        .super_name = (*loaded)->super_name(),
        .instance_field_slots = 0,
        .instance_fields = {},
        .instance_defaults = {},
    };

    if (!result.super_name.empty())
    {
      auto parent = layout(result.super_name);
      if (!parent)
      {
        return std::unexpected(parent.error());
      }
      result.instance_field_slots = (*parent)->instance_field_slots;
      result.instance_fields = (*parent)->instance_fields;
      result.instance_defaults = (*parent)->instance_defaults;
    }

    for (const classfile::Field &field : (*loaded)->fields())
    {
      if ((field.access_flags & kAccStatic) != 0)
      {
        continue;
      }
      auto descriptor = parse_field_descriptor(field.descriptor);
      if (!descriptor)
      {
        return std::unexpected(descriptor.error());
      }
      if (descriptor->kind == JavaTypeKind::void_type)
      {
        return fail(ErrorCode::malformed_class,
                    "instance field cannot use the void descriptor");
      }

      const std::string key = field_key(class_name,
                                        field.name,
                                        field.descriptor);
      auto initial = default_value(field.descriptor);
      if (!initial)
      {
        return std::unexpected(initial.error());
      }
      result.instance_fields.insert_or_assign(key,
                                              result.instance_field_slots);
      result.instance_defaults.push_back(*initial);
      ++result.instance_field_slots;
    }

    return std::make_shared<const ClassLayout>(std::move(result));
  }

  Result<Value> ClassStateRegistry::default_value(
      std::string_view descriptor)
  {
    auto parsed = parse_field_descriptor(descriptor);
    if (!parsed)
    {
      return std::unexpected(parsed.error());
    }

    switch (parsed->kind)
    {
    case JavaTypeKind::boolean:
    case JavaTypeKind::byte:
    case JavaTypeKind::character:
    case JavaTypeKind::short_integer:
    case JavaTypeKind::integer:
      return Value::from_int(0);
    case JavaTypeKind::float32:
      return Value::from_float(0.0F);
    case JavaTypeKind::long_integer:
      return Value::from_long(0);
    case JavaTypeKind::float64:
      return Value::from_double(0.0);
    case JavaTypeKind::reference:
    case JavaTypeKind::array:
      return Value::from_reference({});
    case JavaTypeKind::void_type:
      return fail(ErrorCode::malformed_class,
                  "void is not a valid field descriptor");
    }
    return fail(ErrorCode::internal_error,
                "unknown field descriptor kind");
  }

  std::string ClassStateRegistry::field_key(
      std::string_view declaring_class,
      std::string_view name,
      std::string_view descriptor)
  {
    std::string key;
    key.reserve(declaring_class.size() + name.size() + descriptor.size() + 2);
    key.append(declaring_class);
    key.push_back('#');
    key.append(name);
    key.push_back(':');
    key.append(descriptor);
    return key;
  }

} // namespace phoneme::vm
