#include "phoneme/vm/ClassRepository.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "phoneme/filesystem/FileSystem.hpp"
#include "phoneme/vm/BuiltinClasses.hpp"
#include "phoneme/vm/Descriptor.hpp"
#include "phoneme/vm/PerformanceCounters.hpp"
#include "phoneme/vm/Verifier.hpp"

namespace phoneme::vm
{
  namespace
  {
    [[nodiscard]] std::string method_resolution_key(
        std::string_view owner,
        std::string_view name,
        std::string_view descriptor)
    {
      std::string key;
      key.reserve(owner.size() + name.size() + descriptor.size() + 2U);
      key.append(owner);
      key.push_back('\n');
      key.append(name);
      key.push_back('\n');
      key.append(descriptor);
      return key;
    }

    [[nodiscard]] std::string assignability_key(
        std::string_view source,
        std::string_view target)
    {
      std::string key;
      key.reserve(source.size() + target.size() + 1U);
      key.append(source);
      key.push_back('\n');
      key.append(target);
      return key;
    }
  } // namespace

  Status ClassRepository::add_archive(std::string archive_path)
  {
    if (archive_path.empty())
    {
      return fail(ErrorCode::invalid_argument,
                  "classpath archive path must not be empty");
    }
    auto archive = archive::ZipArchive::open(archive_path);
    if (!archive)
    {
      return std::unexpected(archive.error());
    }

    std::scoped_lock lock(mutex_);
    const auto existing = std::find_if(
        archives_.begin(), archives_.end(),
        [&archive_path](const ClasspathArchive &candidate)
        {
          return candidate.path == archive_path;
        });
    if (existing == archives_.end())
    {
      archives_.push_back(ClasspathArchive{
          .path = std::move(archive_path),
          .archive = std::move(*archive),
      });
      cache_.clear();
      method_cache_.clear();
      declared_method_cache_.clear();
      assignability_cache_.clear();
      metadata_.clear();
    }
    return {};
  }

  Result<std::shared_ptr<const classfile::ClassFile>> ClassRepository::load(
      std::string_view binary_name)
  {
    const std::string internal_name = normalize_name(binary_name);
    if (internal_name.empty())
    {
      return fail(ErrorCode::invalid_argument,
                  "class name must not be empty");
    }

    std::scoped_lock lock(mutex_);
    if (const auto iterator = cache_.find(internal_name);
        iterator != cache_.end())
    {
      PerformanceCounters::record_class_cache(true);
      return iterator->second;
    }
    PerformanceCounters::record_class_cache(false);

    auto loaded = load_uncached(internal_name);
    if (!loaded)
    {
      return std::unexpected(loaded.error());
    }
    auto published = metadata_.publish_class(*loaded);
    if (!published)
    {
      return std::unexpected(published.error());
    }
    cache_.insert_or_assign(internal_name, *loaded);
    return *loaded;
  }

  Result<ResolvedMethod> ClassRepository::resolve_method(
      std::string_view binary_name,
      std::string_view method_name,
      std::string_view descriptor)
  {
    if (method_name.empty() || descriptor.empty())
    {
      return fail(ErrorCode::invalid_argument,
                  "method name and descriptor must not be empty");
    }

    std::string current = normalize_name(binary_name);
    PerformanceCounters::record_metadata_key_construction();
    const std::string cache_key = method_resolution_key(
        current, method_name, descriptor);
    {
      std::scoped_lock lock(mutex_);
      if (const auto cached = method_cache_.find(cache_key);
          cached != method_cache_.end())
      {
        PerformanceCounters::record_method_resolution(true, false);
        return cached->second;
      }
    }
    PerformanceCounters::record_method_resolution(false, false);
    const auto cache_method = [this, &cache_key](ResolvedMethod resolved) {
      std::scoped_lock lock(mutex_);
      const auto [iterator, inserted] = method_cache_.emplace(
          cache_key, std::move(resolved));
      (void)inserted;
      return iterator->second;
    };

    std::unordered_set<std::string> visited;
    visited.reserve(16);
    std::vector<std::string> pending_interfaces;
    pending_interfaces.reserve(8);

    while (!current.empty())
    {
      if (!visited.insert(current).second)
      {
        return fail(ErrorCode::malformed_class,
                    "class hierarchy contains a cycle");
      }
      if (visited.size() > 256)
      {
        return fail(ErrorCode::malformed_class,
                    "class hierarchy exceeds the supported depth");
      }

      auto loaded = load(current);
      if (!loaded)
      {
        return std::unexpected(loaded.error());
      }
      if (const classfile::Method *method =
              (*loaded)->find_method(method_name, descriptor);
          method != nullptr)
      {
        auto runtime = metadata_.publish_method(*loaded, *method);
        if (!runtime)
        {
          return std::unexpected(runtime.error());
        }
        return cache_method(ResolvedMethod{
            .owner = *loaded,
            .method = method,
            .runtime = std::move(*runtime),
        });
      }
      for (const std::string &interface_name : (*loaded)->interfaces())
      {
        pending_interfaces.push_back(interface_name);
      }
      current = (*loaded)->super_name();
    }

    while (!pending_interfaces.empty())
    {
      std::string interface_name = std::move(pending_interfaces.back());
      pending_interfaces.pop_back();
      if (!visited.insert(interface_name).second)
      {
        continue;
      }
      if (visited.size() > 1'024)
      {
        return fail(ErrorCode::malformed_class,
                    "method hierarchy exceeds the supported depth");
      }
      auto loaded = load(interface_name);
      if (!loaded)
      {
        return std::unexpected(loaded.error());
      }
      if (const classfile::Method *method =
              (*loaded)->find_method(method_name, descriptor);
          method != nullptr)
      {
        auto runtime = metadata_.publish_method(*loaded, *method);
        if (!runtime)
        {
          return std::unexpected(runtime.error());
        }
        return cache_method(ResolvedMethod{
            .owner = *loaded,
            .method = method,
            .runtime = std::move(*runtime),
        });
      }
      for (const std::string &parent_interface : (*loaded)->interfaces())
      {
        pending_interfaces.push_back(parent_interface);
      }
    }

    return fail(ErrorCode::method_not_found,
                "method was not found in the class hierarchy: " +
                    normalize_name(binary_name) + "." +
                    std::string(method_name) + std::string(descriptor));
  }

  Result<ResolvedMethod> ClassRepository::resolve_declared_method(
      std::string_view binary_name,
      std::string_view method_name,
      std::string_view descriptor)
  {
    if (method_name.empty() || descriptor.empty())
    {
      return fail(ErrorCode::invalid_argument,
                  "method name and descriptor must not be empty");
    }
    const std::string normalized = normalize_name(binary_name);
    PerformanceCounters::record_metadata_key_construction();
    const std::string cache_key = method_resolution_key(
        normalized, method_name, descriptor);
    {
      std::scoped_lock lock(mutex_);
      if (const auto cached = declared_method_cache_.find(cache_key);
          cached != declared_method_cache_.end())
      {
        PerformanceCounters::record_method_resolution(true, true);
        return cached->second;
      }
    }
    PerformanceCounters::record_method_resolution(false, true);
    auto loaded = load(normalized);
    if (!loaded)
    {
      return std::unexpected(loaded.error());
    }
    const classfile::Method *method =
        (*loaded)->find_method(method_name, descriptor);
    if (method == nullptr)
    {
      return fail(ErrorCode::method_not_found,
                  "method was not declared by the requested class: " +
                      normalize_name(binary_name) + "." +
                      std::string(method_name) + std::string(descriptor));
    }
    auto runtime = metadata_.publish_method(*loaded, *method);
    if (!runtime)
    {
      return std::unexpected(runtime.error());
    }
    ResolvedMethod resolved {
        .owner = *loaded,
        .method = method,
        .runtime = std::move(*runtime),
    };
    std::scoped_lock lock(mutex_);
    const auto [iterator, inserted] = declared_method_cache_.emplace(
        cache_key, std::move(resolved));
    (void)inserted;
    return iterator->second;
  }

  Result<bool> ClassRepository::is_assignable(
      std::string_view source_name,
      std::string_view target_name)
  {
    const std::string source = normalize_name(source_name);
    const std::string target = normalize_name(target_name);
    if (source.empty() || target.empty())
    {
      return fail(ErrorCode::invalid_argument,
                  "assignability requires non-empty class names");
    }
    PerformanceCounters::record_metadata_key_construction();
    const std::string cache_key = assignability_key(source, target);
    {
      std::scoped_lock lock(mutex_);
      if (const auto cached = assignability_cache_.find(cache_key);
          cached != assignability_cache_.end())
      {
        PerformanceCounters::record_assignability_cache(true);
        return cached->second;
      }
    }
    PerformanceCounters::record_assignability_cache(false);
    const auto cache_result = [this, &cache_key](bool value) {
      std::scoped_lock lock(mutex_);
      assignability_cache_.insert_or_assign(cache_key, value);
      return value;
    };
    if (source == target)
    {
      return cache_result(true);
    }

    const auto component_name = [](std::string_view descriptor)
        -> Result<std::string>
    {
      if (descriptor.empty())
      {
        return fail(ErrorCode::malformed_class,
                    "array descriptor has no component type");
      }
      if (descriptor.front() == '[')
      {
        return std::string(descriptor);
      }
      if (descriptor.front() == 'L' && descriptor.size() >= 3 &&
          descriptor.back() == ';')
      {
        return std::string(descriptor.substr(1, descriptor.size() - 2));
      }
      if (descriptor.size() == 1 &&
          std::string_view("ZCBSIFJD").find(descriptor.front()) !=
              std::string_view::npos)
      {
        return std::string(descriptor);
      }
      return fail(ErrorCode::malformed_class,
                  "array descriptor contains an invalid component type");
    };

    if (source.front() == '[')
    {
      if (target == "java/lang/Object" ||
          target == "java/lang/Cloneable" ||
          target == "java/io/Serializable")
      {
        return cache_result(true);
      }
      if (target.front() != '[')
      {
        return cache_result(false);
      }
      auto source_component = component_name(std::string_view(source).substr(1));
      auto target_component = component_name(std::string_view(target).substr(1));
      if (!source_component)
      {
        return std::unexpected(source_component.error());
      }
      if (!target_component)
      {
        return std::unexpected(target_component.error());
      }
      const bool source_primitive = source_component->size() == 1;
      const bool target_primitive = target_component->size() == 1;
      if (source_primitive || target_primitive)
      {
        return cache_result(*source_component == *target_component);
      }
      auto component_assignable = is_assignable(
          *source_component, *target_component);
      if (!component_assignable)
      {
        return std::unexpected(component_assignable.error());
      }
      return cache_result(*component_assignable);
    }
    if (target.front() == '[')
    {
      return cache_result(false);
    }

    std::vector<std::string> pending{source};
    std::unordered_set<std::string> visited;
    visited.reserve(16);
    while (!pending.empty())
    {
      std::string current = std::move(pending.back());
      pending.pop_back();
      if (!visited.insert(current).second)
      {
        continue;
      }
      if (visited.size() > 1'024)
      {
        return fail(ErrorCode::malformed_class,
                    "class hierarchy exceeds the assignability limit");
      }
      if (current == target)
      {
        return cache_result(true);
      }
      auto loaded = load(current);
      if (!loaded)
      {
        return std::unexpected(loaded.error());
      }
      if (!(*loaded)->super_name().empty())
      {
        pending.push_back((*loaded)->super_name());
      }
      for (const std::string &interface_name : (*loaded)->interfaces())
      {
        pending.push_back(interface_name);
      }
    }
    return cache_result(false);
  }

  Result<std::vector<u8>> ClassRepository::read_resource(
      std::string_view resource_name) const
  {
    auto normalized = filesystem::normalize_resource_path(resource_name);
    if (!normalized)
    {
      return std::unexpected(normalized.error());
    }

    std::scoped_lock lock(mutex_);
    for (const ClasspathArchive &classpath_archive : archives_)
    {
      const archive::ZipEntry *entry =
          classpath_archive.archive.find(*normalized);
      if (entry == nullptr || entry->name.ends_with('/'))
      {
        continue;
      }
      return classpath_archive.archive.read(*entry);
    }
    return fail(ErrorCode::class_not_found,
                "resource is not present on the application classpath: " +
                    *normalized);
  }

  void ClassRepository::clear() noexcept
  {
    std::scoped_lock lock(mutex_);
    archives_.clear();
    cache_.clear();
    method_cache_.clear();
    declared_method_cache_.clear();
    assignability_cache_.clear();
    metadata_.clear();
  }

  Result<std::shared_ptr<const classfile::ClassFile>>
  ClassRepository::load_uncached(std::string_view internal_name) const
  {
    if (!internal_name.empty() && internal_name.front() == '[')
    {
      auto descriptor = parse_field_descriptor(internal_name);
      if (!descriptor || descriptor->kind != JavaTypeKind::array)
      {
        return fail(ErrorCode::malformed_class,
                    "invalid array class descriptor: " +
                        std::string(internal_name));
      }
      constexpr u16 kArrayFlags = 0x0001U | 0x0010U | 0x0400U;
      return std::make_shared<const classfile::ClassFile>(
          classfile::ClassFile::builtin(
              std::string(internal_name),
              "java/lang/Object",
              kArrayFlags,
              {},
              {},
              {"java/lang/Cloneable", "java/io/Serializable"}));
    }
    // A few vendor APIs are commonly bundled as application-specific
    // compatibility wrappers. Prefer those JAR-local implementations and use
    // Core's vendor classes only as a fallback when the application omitted
    // them. Standard CLDC/MIDP classes remain protected from shadowing.
    const bool prefer_archive =
        internal_name.starts_with("com/sprintpcs/media/") ||
        internal_name == "com/samsung/util/AudioClip";
    if (!prefer_archive)
    {
      if (auto builtin = load_builtin_class(internal_name); builtin)
      {
        return builtin;
      }
    }

    const std::string entry_name = std::string(internal_name) + ".class";
    for (const ClasspathArchive &classpath_archive : archives_)
    {
      const archive::ZipEntry *entry =
          classpath_archive.archive.find(entry_name);
      if (entry == nullptr)
      {
        continue;
      }
      auto bytes = classpath_archive.archive.read(*entry);
      if (!bytes)
      {
        return std::unexpected(bytes.error());
      }
      auto parsed = classfile::ClassFile::parse(*bytes);
      if (!parsed)
      {
        return std::unexpected(parsed.error());
      }
      if (parsed->name() != internal_name)
      {
        return fail(ErrorCode::malformed_class,
                    "classpath entry name does not match class declaration");
      }
      auto verified = verify_class(*parsed);
      if (!verified)
      {
        return fail(verified.error().code,
                    std::string(internal_name) + "." +
                        verified.error().message);
      }
      return std::make_shared<const classfile::ClassFile>(std::move(*parsed));
    }

    if (prefer_archive)
    {
      if (auto builtin = load_builtin_class(internal_name); builtin)
      {
        return builtin;
      }
    }

    return fail(ErrorCode::class_not_found,
                "class is neither built into Core nor present in the application JAR: " +
                    std::string(internal_name));
  }

  std::string ClassRepository::normalize_name(std::string_view binary_name)
  {
    std::string normalized(binary_name);
    std::replace(normalized.begin(), normalized.end(), '.', '/');
    while (!normalized.empty() && normalized.front() == '/')
    {
      normalized.erase(normalized.begin());
    }
    return normalized;
  }

} // namespace phoneme::vm
