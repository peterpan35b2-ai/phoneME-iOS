#pragma once

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/platform/MappedFile.hpp"

namespace phoneme::archive {

struct ZipEntry final {
    // `name` is the logical archive-root path used by J2ME resource lookup.
    // `stored_name` preserves the exact ZIP header spelling for integrity
    // checks (some legacy JARs prefix resource names with '/').
    std::string name;
    std::string stored_name;
    u16 compression_method {0};
    u16 flags {0};
    u32 crc32 {0};
    u64 compressed_size {0};
    u64 uncompressed_size {0};
    u64 local_header_offset {0};
};

struct ZipLimits final {
    u64 maximum_archive_bytes {128U * 1024U * 1024U};
    usize maximum_entries {10'000U};
    u64 maximum_entry_uncompressed_bytes {64U * 1024U * 1024U};
    u64 maximum_total_uncompressed_bytes {256U * 1024U * 1024U};
    u64 compression_ratio_threshold_bytes {1024U * 1024U};
    u32 maximum_compression_ratio {200U};
    usize maximum_entry_name_bytes {1024U};
    bool reject_unsafe_paths {true};
    bool reject_duplicate_names {true};
};

class ZipArchive final {
public:
    ZipArchive() = default;

    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;
    ZipArchive(ZipArchive&&) noexcept = default;
    ZipArchive& operator=(ZipArchive&&) noexcept = default;

    [[nodiscard]] static Result<ZipArchive> open(const std::string& path);
    [[nodiscard]] static Result<ZipArchive> open(const std::string& path,
                                                 const ZipLimits& limits);

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
    struct TransparentStringHash final {
        using is_transparent = void;

        [[nodiscard]] usize operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view> {}(value);
        }

        [[nodiscard]] usize operator()(const std::string& value) const noexcept {
            return (*this)(std::string_view(value));
        }
    };

    explicit ZipArchive(platform::MappedFile file, ZipLimits limits)
        : file_(std::move(file)), limits_(limits) {}

    [[nodiscard]] Status parse_directory();

    platform::MappedFile file_;
    ZipLimits limits_;
    usize central_directory_offset_ {0};
    std::vector<ZipEntry> entries_;
    std::unordered_map<std::string, usize,
                       TransparentStringHash, std::equal_to<>> entry_index_;
};

[[nodiscard]] Result<std::vector<u8>> inflate_raw(std::span<const u8> source,
                                                   usize output_size);
[[nodiscard]] Result<std::vector<u8>> inflate_zlib(std::span<const u8> source,
                                                    usize output_size);
[[nodiscard]] u32 crc32(std::span<const u8> bytes) noexcept;

} // namespace phoneme::archive
