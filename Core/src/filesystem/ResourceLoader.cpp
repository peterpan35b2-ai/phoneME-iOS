#include "phoneme/filesystem/ResourceLoader.hpp"

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/filesystem/FileSystem.hpp"

namespace phoneme::filesystem {

Result<std::string> ResourceLoader::resolve_class_resource(
    std::string_view class_name,
    std::string_view resource_name) {
    std::string path;
    if (!resource_name.empty() &&
        (resource_name.front() == '/' || resource_name.front() == '\\')) {
        path.assign(resource_name.begin() + 1, resource_name.end());
    } else {
        const usize slash = class_name.rfind('/');
        if (slash != std::string_view::npos &&
            (class_name.empty() || class_name.front() != '[')) {
            path.assign(class_name.substr(0, slash + 1U));
        }
        path.append(resource_name);
    }
    return normalize_resource_path(path);
}

Result<std::vector<u8>> ResourceLoader::read(
    std::span<const std::string> archive_paths,
    std::string_view resource_path) {
    auto normalized = normalize_resource_path(resource_path);
    if (!normalized) return std::unexpected(normalized.error());

    for (const std::string& archive_path : archive_paths) {
        auto archive = archive::ZipArchive::open(archive_path);
        if (!archive) return std::unexpected(archive.error());
        const archive::ZipEntry* entry = archive->find(*normalized);
        if (entry == nullptr || entry->name.ends_with('/')) continue;
        return archive->read(*entry);
    }
    return fail(ErrorCode::class_not_found,
                "resource is not present on the application classpath: " +
                    *normalized);
}

Result<std::vector<u8>> ResourceLoader::read_for_class(
    std::span<const std::string> archive_paths,
    std::string_view class_name,
    std::string_view resource_name) {
    auto resolved = resolve_class_resource(class_name, resource_name);
    if (!resolved) return std::unexpected(resolved.error());
    return read(archive_paths, *resolved);
}

} // namespace phoneme::filesystem
