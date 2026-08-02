#include "phoneme/filesystem/ResourceLoader.hpp"

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/filesystem/FileSystem.hpp"

namespace phoneme::filesystem {

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

} // namespace phoneme::filesystem
