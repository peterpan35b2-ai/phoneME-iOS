#include "phoneme/archive/ZipArchive.hpp"

#include <algorithm>
#include <climits>
#include <limits>
#include <utility>

#include <zlib.h>

#include "phoneme/base/ByteReader.hpp"
#include "phoneme/base/Checked.hpp"

namespace phoneme::archive {
namespace {

constexpr u32 kEndOfCentralDirectorySignature = 0x06054B50U;
constexpr u32 kCentralDirectorySignature = 0x02014B50U;
constexpr u32 kLocalFileHeaderSignature = 0x04034B50U;
constexpr usize kEndOfCentralDirectorySize = 22;
constexpr usize kMaximumZipCommentSize = 65'535;
constexpr usize kCentralDirectoryHeaderSize = 46;
constexpr usize kLocalFileHeaderSize = 30;
constexpr u16 kEncryptedFlag = 1U << 0U;

[[nodiscard]] u16 read_le_u16_at(std::span<const u8> bytes, usize offset) {
    return static_cast<u16>(static_cast<u16>(bytes[offset]) |
                            (static_cast<u16>(bytes[offset + 1]) << 8U));
}

[[nodiscard]] u32 read_le_u32_at(std::span<const u8> bytes, usize offset) {
    return static_cast<u32>(bytes[offset]) |
           (static_cast<u32>(bytes[offset + 1]) << 8U) |
           (static_cast<u32>(bytes[offset + 2]) << 16U) |
           (static_cast<u32>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] Result<usize> require_range(std::span<const u8> bytes,
                                          usize offset,
                                          usize length) {
    auto end = checked_add(offset, length);
    if (!end || *end > bytes.size()) {
        return fail(ErrorCode::malformed_archive, "ZIP structure exceeds file bounds");
    }
    return *end;
}

[[nodiscard]] Result<std::vector<u8>> inflate_impl(std::span<const u8> source,
                                                    usize output_size,
                                                    int window_bits) {
    if (output_size == 0) {
        return std::vector<u8> {};
    }

    std::vector<u8> output(output_size);
    z_stream stream {};
    const int initialization = ::inflateInit2(&stream, window_bits);
    if (initialization != Z_OK) {
        return fail(ErrorCode::internal_error, "zlib initialization failed");
    }

    usize input_offset = 0;
    usize output_offset = 0;
    int status = Z_OK;

    while (status == Z_OK) {
        if (stream.avail_in == 0 && input_offset < source.size()) {
            const usize remaining = source.size() - input_offset;
            const usize chunk = std::min<usize>(remaining, std::numeric_limits<uInt>::max());
            stream.next_in = const_cast<Bytef*>(
                reinterpret_cast<const Bytef*>(source.data() + input_offset));
            stream.avail_in = static_cast<uInt>(chunk);
            input_offset += chunk;
        }

        if (stream.avail_out == 0 && output_offset < output.size()) {
            const usize remaining = output.size() - output_offset;
            const usize chunk = std::min<usize>(remaining, std::numeric_limits<uInt>::max());
            stream.next_out = reinterpret_cast<Bytef*>(output.data() + output_offset);
            stream.avail_out = static_cast<uInt>(chunk);
            output_offset += chunk;
        }

        const uInt output_before = stream.avail_out;
        status = ::inflate(&stream, Z_NO_FLUSH);
        const usize produced = static_cast<usize>(output_before - stream.avail_out);

        if (status == Z_BUF_ERROR && produced == 0) {
            break;
        }
        if (stream.avail_in == 0 && input_offset >= source.size() && produced == 0 &&
            status != Z_STREAM_END) {
            break;
        }
    }

    const uLong total_output = stream.total_out;
    const int finalization = ::inflateEnd(&stream);
    if (finalization != Z_OK) {
        return fail(ErrorCode::internal_error, "zlib finalization failed");
    }
    if (status != Z_STREAM_END) {
        return fail(ErrorCode::malformed_archive, "deflate stream ended unexpectedly");
    }
    if (static_cast<u64>(total_output) != static_cast<u64>(output_size)) {
        return fail(ErrorCode::malformed_archive, "deflate output length mismatch");
    }

    return output;
}

} // namespace

Result<ZipArchive> ZipArchive::open(const std::string& path) {
    auto file = platform::MappedFile::open_readonly(path);
    if (!file) {
        return std::unexpected(file.error());
    }

    ZipArchive archive(std::move(*file));
    auto parsed = archive.parse_directory();
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    return archive;
}

Status ZipArchive::parse_directory() {
    const auto bytes = file_.bytes();
    if (bytes.size() < kEndOfCentralDirectorySize) {
        return fail(ErrorCode::malformed_archive, "ZIP file is too small");
    }

    const usize minimum_offset = bytes.size() >
            (kEndOfCentralDirectorySize + kMaximumZipCommentSize)
        ? bytes.size() - kEndOfCentralDirectorySize - kMaximumZipCommentSize
        : 0;

    usize eocd_offset = bytes.size() - kEndOfCentralDirectorySize;
    bool found = false;
    for (;;) {
        if (read_le_u32_at(bytes, eocd_offset) == kEndOfCentralDirectorySignature) {
            const u16 comment_length = read_le_u16_at(bytes, eocd_offset + 20);
            auto expected_end = checked_add(eocd_offset,
                                            kEndOfCentralDirectorySize +
                                                static_cast<usize>(comment_length));
            if (expected_end && *expected_end == bytes.size()) {
                found = true;
                break;
            }
        }
        if (eocd_offset == minimum_offset) {
            break;
        }
        --eocd_offset;
    }

    if (!found) {
        return fail(ErrorCode::malformed_archive,
                    "ZIP end-of-central-directory record was not found");
    }

    const u16 disk_number = read_le_u16_at(bytes, eocd_offset + 4);
    const u16 directory_disk = read_le_u16_at(bytes, eocd_offset + 6);
    const u16 entries_on_disk = read_le_u16_at(bytes, eocd_offset + 8);
    const u16 total_entries = read_le_u16_at(bytes, eocd_offset + 10);
    const u32 directory_size_32 = read_le_u32_at(bytes, eocd_offset + 12);
    const u32 directory_offset_32 = read_le_u32_at(bytes, eocd_offset + 16);

    if (disk_number != 0 || directory_disk != 0 || entries_on_disk != total_entries) {
        return fail(ErrorCode::unsupported_archive,
                    "multi-disk ZIP archives are not supported");
    }
    if (total_entries == std::numeric_limits<u16>::max() ||
        directory_size_32 == std::numeric_limits<u32>::max() ||
        directory_offset_32 == std::numeric_limits<u32>::max()) {
        return fail(ErrorCode::unsupported_archive,
                    "ZIP64 JAR archives are not supported yet");
    }

    const usize directory_offset = static_cast<usize>(directory_offset_32);
    const usize directory_size = static_cast<usize>(directory_size_32);
    auto directory_end = require_range(bytes, directory_offset, directory_size);
    if (!directory_end || *directory_end > eocd_offset) {
        return fail(ErrorCode::malformed_archive,
                    "ZIP central directory has an invalid range");
    }

    entries_.clear();
    entries_.reserve(total_entries);
    usize cursor = directory_offset;

    for (u16 index = 0; index < total_entries; ++index) {
        auto fixed_end = require_range(bytes, cursor, kCentralDirectoryHeaderSize);
        if (!fixed_end) {
            return std::unexpected(fixed_end.error());
        }
        if (read_le_u32_at(bytes, cursor) != kCentralDirectorySignature) {
            return fail(ErrorCode::malformed_archive,
                        "invalid ZIP central-directory signature");
        }

        const u16 flags = read_le_u16_at(bytes, cursor + 8);
        const u16 method = read_le_u16_at(bytes, cursor + 10);
        const u32 entry_crc = read_le_u32_at(bytes, cursor + 16);
        const u32 compressed_size = read_le_u32_at(bytes, cursor + 20);
        const u32 uncompressed_size = read_le_u32_at(bytes, cursor + 24);
        const u16 name_length = read_le_u16_at(bytes, cursor + 28);
        const u16 extra_length = read_le_u16_at(bytes, cursor + 30);
        const u16 comment_length = read_le_u16_at(bytes, cursor + 32);
        const u16 start_disk = read_le_u16_at(bytes, cursor + 34);
        const u32 local_offset = read_le_u32_at(bytes, cursor + 42);

        if ((flags & kEncryptedFlag) != 0) {
            return fail(ErrorCode::unsupported_archive,
                        "encrypted JAR entries are not supported");
        }
        if (start_disk != 0) {
            return fail(ErrorCode::unsupported_archive,
                        "multi-disk JAR entries are not supported");
        }
        if (compressed_size == std::numeric_limits<u32>::max() ||
            uncompressed_size == std::numeric_limits<u32>::max() ||
            local_offset == std::numeric_limits<u32>::max()) {
            return fail(ErrorCode::unsupported_archive,
                        "ZIP64 JAR entries are not supported yet");
        }

        const usize variable_size = static_cast<usize>(name_length) +
                                    static_cast<usize>(extra_length) +
                                    static_cast<usize>(comment_length);
        auto record_end = require_range(bytes, *fixed_end, variable_size);
        if (!record_end || *record_end > *directory_end) {
            return fail(ErrorCode::malformed_archive,
                        "ZIP central-directory entry exceeds directory bounds");
        }

        const auto name_bytes = bytes.subspan(*fixed_end, name_length);
        std::string name(reinterpret_cast<const char*>(name_bytes.data()),
                         name_bytes.size());
        if (name.find('\0') != std::string::npos) {
            return fail(ErrorCode::malformed_archive,
                        "JAR entry name contains a null byte");
        }

        entries_.push_back(ZipEntry {
            .name = std::move(name),
            .compression_method = method,
            .flags = flags,
            .crc32 = entry_crc,
            .compressed_size = compressed_size,
            .uncompressed_size = uncompressed_size,
            .local_header_offset = local_offset,
        });

        cursor = *record_end;
    }

    if (cursor != *directory_end) {
        return fail(ErrorCode::malformed_archive,
                    "ZIP central-directory size does not match its entries");
    }

    return {};
}

const ZipEntry* ZipArchive::find(std::string_view name) const noexcept {
    const auto iterator = std::find_if(entries_.begin(), entries_.end(),
                                       [name](const ZipEntry& entry) {
                                           return entry.name == name;
                                       });
    return iterator == entries_.end() ? nullptr : &*iterator;
}

Result<std::vector<u8>> ZipArchive::read(std::string_view name) const {
    const ZipEntry* entry = find(name);
    if (entry == nullptr) {
        return fail(ErrorCode::class_not_found,
                    "JAR entry was not found: " + std::string(name));
    }
    return read(*entry);
}

Result<std::vector<u8>> ZipArchive::read(const ZipEntry& entry) const {
    const auto bytes = file_.bytes();
    auto local_offset = checked_narrow<usize>(entry.local_header_offset);
    if (!local_offset) {
        return std::unexpected(local_offset.error());
    }

    auto fixed_end = require_range(bytes, *local_offset, kLocalFileHeaderSize);
    if (!fixed_end) {
        return std::unexpected(fixed_end.error());
    }
    if (read_le_u32_at(bytes, *local_offset) != kLocalFileHeaderSignature) {
        return fail(ErrorCode::malformed_archive,
                    "invalid ZIP local-file-header signature");
    }

    const u16 local_flags = read_le_u16_at(bytes, *local_offset + 6);
    const u16 local_method = read_le_u16_at(bytes, *local_offset + 8);
    const u16 name_length = read_le_u16_at(bytes, *local_offset + 26);
    const u16 extra_length = read_le_u16_at(bytes, *local_offset + 28);

    if ((local_flags & kEncryptedFlag) != 0 || local_method != entry.compression_method) {
        return fail(ErrorCode::malformed_archive,
                    "ZIP local header conflicts with central directory");
    }

    auto data_offset = checked_add(*fixed_end,
                                   static_cast<usize>(name_length) +
                                       static_cast<usize>(extra_length));
    auto compressed_size = checked_narrow<usize>(entry.compressed_size);
    auto uncompressed_size = checked_narrow<usize>(entry.uncompressed_size);
    if (!data_offset || !compressed_size || !uncompressed_size) {
        return fail(ErrorCode::overflow, "JAR entry size does not fit the platform");
    }

    auto data_end = require_range(bytes, *data_offset, *compressed_size);
    if (!data_end) {
        return std::unexpected(data_end.error());
    }
    const auto compressed = bytes.subspan(*data_offset, *compressed_size);

    Result<std::vector<u8>> output = [&]() -> Result<std::vector<u8>> {
        switch (entry.compression_method) {
        case 0:
            if (*compressed_size != *uncompressed_size) {
                return fail(ErrorCode::malformed_archive,
                            "stored JAR entry has mismatched lengths");
            }
            return std::vector<u8>(compressed.begin(), compressed.end());
        case 8:
            return inflate_raw(compressed, *uncompressed_size);
        default:
            return fail(ErrorCode::unsupported_archive,
                        "unsupported JAR compression method");
        }
    }();

    if (!output) {
        return output;
    }
    if (crc32(*output) != entry.crc32) {
        return fail(ErrorCode::checksum_mismatch, "JAR entry CRC32 mismatch");
    }
    return output;
}

Result<std::vector<u8>> inflate_raw(std::span<const u8> source,
                                    usize output_size) {
    return inflate_impl(source, output_size, -MAX_WBITS);
}

Result<std::vector<u8>> inflate_zlib(std::span<const u8> source,
                                     usize output_size) {
    return inflate_impl(source, output_size, MAX_WBITS);
}

u32 crc32(std::span<const u8> bytes) noexcept {
    uLong value = ::crc32(0L, Z_NULL, 0);
    usize offset = 0;
    while (offset < bytes.size()) {
        const usize remaining = bytes.size() - offset;
        const usize chunk = std::min<usize>(remaining,
                                            std::numeric_limits<uInt>::max());
        value = ::crc32(value,
                        reinterpret_cast<const Bytef*>(bytes.data() + offset),
                        static_cast<uInt>(chunk));
        offset += chunk;
    }
    return static_cast<u32>(value);
}

} // namespace phoneme::archive
