#include "phoneme/vm/ClassRepository.hpp"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "phoneme/filesystem/ResourceLoader.hpp"
#include "phoneme/vm/BuiltinClasses.hpp"
#include "phoneme/vm/Verifier.hpp"

namespace phoneme::vm
{

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
    if (std::find(archive_paths_.begin(), archive_paths_.end(), archive_path) ==
        archive_paths_.end())
    {
      archive_paths_.push_back(std::move(archive_path));
      cache_.clear();
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
      return iterator->second;
    }

    auto loaded = load_uncached(internal_name);
    if (!loaded)
    {
      return std::unexpected(loaded.error());
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
        return ResolvedMethod{
            .owner = *loaded,
            .method = method,
        };
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
        return ResolvedMethod{
            .owner = *loaded,
            .method = method,
        };
      }
      for (const std::string &parent_interface : (*loaded)->interfaces())
      {
        pending_interfaces.push_back(parent_interface);
      }
    }

    return fail(ErrorCode::method_not_found,
                "method was not found in the class hierarchy");
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
    auto loaded = load(binary_name);
    if (!loaded)
    {
      return std::unexpected(loaded.error());
    }
    const classfile::Method *method =
        (*loaded)->find_method(method_name, descriptor);
    if (method == nullptr)
    {
      return fail(ErrorCode::method_not_found,
                  "method was not declared by the requested class");
    }
    return ResolvedMethod{
        .owner = *loaded,
        .method = method,
    };
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
    if (source == target)
    {
      return true;
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
        return true;
      }
      if (target.front() != '[')
      {
        return false;
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
        return *source_component == *target_component;
      }
      return is_assignable(*source_component, *target_component);
    }
    if (target.front() == '[')
    {
      return false;
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
        return true;
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
    return false;
  }

  Result<std::vector<u8>> ClassRepository::read_resource(
      std::string_view resource_name) const
  {
    std::scoped_lock lock(mutex_);
    return filesystem::ResourceLoader::read(archive_paths_, resource_name);
  }

  void ClassRepository::clear() noexcept
  {
    std::scoped_lock lock(mutex_);
    archive_paths_.clear();
    cache_.clear();
  }

  Result<std::shared_ptr<const classfile::ClassFile>>
  ClassRepository::load_uncached(std::string_view internal_name) const
  {
    if (auto builtin = load_builtin_class(internal_name); builtin)
    {
      return builtin;
    }

    const std::string entry_name = std::string(internal_name) + ".class";
    for (const std::string &archive_path : archive_paths_)
    {
      auto archive = archive::ZipArchive::open(archive_path);
      if (!archive)
      {
        return std::unexpected(archive.error());
      }
      const archive::ZipEntry *entry = archive->find(entry_name);
      if (entry == nullptr)
      {
        continue;
      }
      auto bytes = archive->read(*entry);
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
        return std::unexpected(verified.error());
      }
      return std::make_shared<const classfile::ClassFile>(std::move(*parsed));
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
