#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/platform/MappedFile.hpp"

namespace phoneme::archive {

struct ZipEntry final {
    std::string name;
    u16 compression_method {0};
    u16 flags {0};
    u32 crc32 {0};
    u64 compressed_size {0};
    u64 uncompressed_size {0};
    u64 local_header_offset {0};
};

class ZipArchive final {
public:
    ZipArchive() = default;

    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;
    ZipArchive(ZipArchive&&) noexcept = default;
    ZipArchive& operator=(ZipArchive&&) noexcept = default;

    [[nodiscard]] static Result<ZipArchive> open(const std::string& path);

    [[nodiscard]] const std::vector<ZipEntry>& entries() const noexcept {
        return entries_;
    }

    [[nodiscard]] const ZipEntry* find(std::string_view name) const noexcept;
    [[nodiscard]] Result<std::vector<u8>> read(const ZipEntry& entry) const;
    [[nodiscard]] Result<std::vector<u8>> read(std::string_view name) const;

    [[nodiscard]] std::span<const u8> raw_bytes() const noexcept {
        return file_.bytes();
    }

private:
    explicit ZipArchive(platform::MappedFile file) : file_(std::move(file)) {}

    [[nodiscard]] Status parse_directory();

    platform::MappedFile file_;
    std::vector<ZipEntry> entries_;
};

[[nodiscard]] Result<std::vector<u8>> inflate_raw(std::span<const u8> source,
                                                   usize output_size);
[[nodiscard]] Result<std::vector<u8>> inflate_zlib(std::span<const u8> source,
                                                    usize output_size);
[[nodiscard]] u32 crc32(std::span<const u8> bytes) noexcept;

} // namespace phoneme::archive
