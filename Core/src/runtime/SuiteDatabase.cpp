#include "phoneme/runtime/SuiteDatabase.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "phoneme/archive/ZipArchive.hpp"
#include "phoneme/base/Checked.hpp"

namespace phoneme::runtime {
namespace {

constexpr std::array<u8, 8> kMagic {'P', 'M', 'E', 'S', 'D', 'B', '2', 0};
constexpr u32 kFormatVersion = 2U;
constexpr usize kMaximumDatabaseBytes = 16U * 1024U * 1024U;
constexpr u32 kMaximumRecords = 4096U;
constexpr u32 kMaximumVectorItems = 16'384U;
constexpr u32 kMaximumProperties = 4096U;
constexpr u32 kMaximumStringBytes = 1024U * 1024U;

void append_u8(std::vector<u8>& output, u8 value) {
    output.push_back(value);
}

void append_u32(std::vector<u8>& output, u32 value) {
    output.push_back(static_cast<u8>(value));
    output.push_back(static_cast<u8>(value >> 8U));
    output.push_back(static_cast<u8>(value >> 16U));
    output.push_back(static_cast<u8>(value >> 24U));
}

void append_u64(std::vector<u8>& output, u64 value) {
    for (u32 shift = 0; shift < 64U; shift += 8U) {
        output.push_back(static_cast<u8>(value >> shift));
    }
}

[[nodiscard]] Status append_string(std::vector<u8>& output,
                                   std::string_view value) {
    if (value.size() > kMaximumStringBytes ||
        value.size() > static_cast<usize>(std::numeric_limits<u32>::max())) {
        return fail(ErrorCode::out_of_range,
                    "suite database string exceeds configured size limit");
    }
    append_u32(output, static_cast<u32>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
    return {};
}

[[nodiscard]] Status append_strings(std::vector<u8>& output,
                                    const std::vector<std::string>& values) {
    if (values.size() > kMaximumVectorItems) {
        return fail(ErrorCode::out_of_range,
                    "suite database vector exceeds configured item limit");
    }
    append_u32(output, static_cast<u32>(values.size()));
    for (const std::string& value : values) {
        auto appended = append_string(output, value);
        if (!appended) {
            return appended;
        }
    }
    return {};
}

class Reader final {
public:
    explicit Reader(std::span<const u8> bytes) : bytes_(bytes) {}

    [[nodiscard]] Result<u8> read_u8() {
        if (offset_ >= bytes_.size()) {
            return fail(ErrorCode::malformed_archive,
                        "suite database ended unexpectedly");
        }
        return bytes_[offset_++];
    }

    [[nodiscard]] Result<u32> read_u32() {
        auto bytes = read_bytes(4U);
        if (!bytes) return std::unexpected(bytes.error());
        return static_cast<u32>((*bytes)[0]) |
               (static_cast<u32>((*bytes)[1]) << 8U) |
               (static_cast<u32>((*bytes)[2]) << 16U) |
               (static_cast<u32>((*bytes)[3]) << 24U);
    }

    [[nodiscard]] Result<u64> read_u64() {
        auto bytes = read_bytes(8U);
        if (!bytes) return std::unexpected(bytes.error());
        u64 value = 0;
        for (u32 index = 0; index < 8U; ++index) {
            value |= static_cast<u64>((*bytes)[index]) << (index * 8U);
        }
        return value;
    }

    [[nodiscard]] Result<std::span<const u8>> read_bytes(usize count) {
        auto end = checked_add(offset_, count);
        if (!end || *end > bytes_.size()) {
            return fail(ErrorCode::malformed_archive,
                        "suite database field exceeds file bounds");
        }
        const auto result = bytes_.subspan(offset_, count);
        offset_ = *end;
        return result;
    }

    [[nodiscard]] Result<std::string> read_string() {
        auto length = read_u32();
        if (!length) return std::unexpected(length.error());
        if (*length > kMaximumStringBytes) {
            return fail(ErrorCode::out_of_range,
                        "suite database string exceeds configured size limit");
        }
        auto bytes = read_bytes(static_cast<usize>(*length));
        if (!bytes) return std::unexpected(bytes.error());
        if (std::find(bytes->begin(), bytes->end(), 0U) != bytes->end()) {
            return fail(ErrorCode::malformed_archive,
                        "suite database string contains a null byte");
        }
        return std::string(reinterpret_cast<const char*>(bytes->data()),
                           bytes->size());
    }

    [[nodiscard]] usize offset() const noexcept { return offset_; }

private:
    std::span<const u8> bytes_;
    usize offset_ {0};
};

[[nodiscard]] Result<std::vector<std::string>> read_strings(Reader& reader) {
    auto count = reader.read_u32();
    if (!count) return std::unexpected(count.error());
    if (*count > kMaximumVectorItems) {
        return fail(ErrorCode::out_of_range,
                    "suite database vector exceeds configured item limit");
    }
    std::vector<std::string> values;
    values.reserve(*count);
    for (u32 index = 0; index < *count; ++index) {
        auto value = reader.read_string();
        if (!value) return std::unexpected(value.error());
        values.push_back(std::move(*value));
    }
    return values;
}

[[nodiscard]] Result<std::vector<u8>> serialize(
    const SuiteDatabaseSnapshot& snapshot) {
    if (snapshot.records.size() > kMaximumRecords) {
        return fail(ErrorCode::out_of_range,
                    "suite database contains too many records");
    }

    std::vector<u8> output;
    output.reserve(4096U);
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    append_u32(output, kFormatVersion);
    append_u64(output, snapshot.generation);
    append_u32(output, static_cast<u32>(snapshot.records.size()));

    for (const SuiteDatabaseRecord& record : snapshot.records) {
        if (!record.id.valid()) {
            return fail(ErrorCode::invalid_argument,
                        "suite database record has an invalid suite ID");
        }
        append_u32(output, static_cast<u32>(record.id.value));
        for (const std::string_view value : {
                 std::string_view(record.identity_key),
                 std::string_view(record.name),
                 std::string_view(record.vendor),
                 std::string_view(record.version),
                 std::string_view(record.jar_relative_path),
                 std::string_view(record.jad_relative_path)}) {
            auto appended = append_string(output, value);
            if (!appended) return std::unexpected(appended.error());
        }
        output.insert(output.end(), record.identity_sha256.begin(),
                      record.identity_sha256.end());
        output.insert(output.end(), record.archive_sha256.begin(),
                      record.archive_sha256.end());
        append_u32(output, record.archive_crc32);
        append_u64(output, record.archive_size);
        append_u64(output, record.declared_jar_size);
        append_u8(output, record.has_permission_declarations ? 1U : 0U);

        auto midlets = append_strings(output, record.midlet_classes);
        if (!midlets) return std::unexpected(midlets.error());
        auto permissions = append_strings(output, record.declared_permissions);
        if (!permissions) return std::unexpected(permissions.error());

        if (record.properties.size() > kMaximumProperties) {
            return fail(ErrorCode::out_of_range,
                        "suite database contains too many properties");
        }
        std::vector<std::pair<std::string_view, std::string_view>> properties;
        properties.reserve(record.properties.size());
        for (const auto& [key, value] : record.properties) {
            properties.emplace_back(key, value);
        }
        std::sort(properties.begin(), properties.end(),
                  [](const auto& left, const auto& right) {
                      return left.first < right.first;
                  });
        append_u32(output, static_cast<u32>(properties.size()));
        for (const auto& [key, value] : properties) {
            auto key_status = append_string(output, key);
            if (!key_status) return std::unexpected(key_status.error());
            auto value_status = append_string(output, value);
            if (!value_status) return std::unexpected(value_status.error());
        }
    }

    if (output.size() > kMaximumDatabaseBytes - sizeof(u32)) {
        return fail(ErrorCode::out_of_range,
                    "suite database exceeds configured size limit");
    }
    append_u32(output, archive::crc32(output));
    return output;
}

[[nodiscard]] Status write_all(int descriptor, std::span<const u8> bytes) {
    usize offset = 0;
    while (offset < bytes.size()) {
        const usize remaining = bytes.size() - offset;
        const usize chunk = std::min<usize>(
            remaining, static_cast<usize>(std::numeric_limits<ssize_t>::max()));
        const ssize_t written = ::write(descriptor, bytes.data() + offset, chunk);
        if (written < 0) {
            if (errno == EINTR) continue;
            return fail(ErrorCode::io_error,
                        "unable to write suite database: " +
                            std::string(std::strerror(errno)));
        }
        if (written == 0) {
            return fail(ErrorCode::io_error,
                        "suite database write made no progress");
        }
        offset += static_cast<usize>(written);
    }
    return {};
}

[[nodiscard]] Status fsync_directory(const std::filesystem::path& directory) {
    const int descriptor = ::open(directory.c_str(), O_RDONLY);
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "unable to open suite database directory for sync");
    }
    const int result = ::fsync(descriptor);
    const int saved_errno = errno;
    static_cast<void>(::close(descriptor));
    if (result != 0) {
        return fail(ErrorCode::io_error,
                    "unable to sync suite database directory: " +
                        std::string(std::strerror(saved_errno)));
    }
    return {};
}

[[nodiscard]] Result<std::vector<u8>> read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        return fail(ErrorCode::io_error, "unable to open suite database: " + path);
    }
    const std::streampos end = stream.tellg();
    if (end < 0) {
        return fail(ErrorCode::io_error, "unable to determine suite database size");
    }
    const auto size64 = static_cast<u64>(end);
    if (size64 > kMaximumDatabaseBytes) {
        return fail(ErrorCode::out_of_range,
                    "suite database exceeds configured size limit");
    }
    auto size = checked_narrow<usize>(size64);
    if (!size) return std::unexpected(size.error());

    std::vector<u8> bytes(*size);
    stream.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        stream.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(bytes.size()));
        if (!stream || static_cast<usize>(stream.gcount()) != bytes.size()) {
            return fail(ErrorCode::io_error,
                        "unable to read complete suite database");
        }
    }
    return bytes;
}

} // namespace

Result<SuiteDatabaseSnapshot> SuiteDatabase::load(
    bool recover_from_backup) const {
    if (path_.empty()) {
        return fail(ErrorCode::not_configured,
                    "suite database path is not configured");
    }

    const std::string backup_path = path_ + ".bak";
    std::error_code error;
    const bool primary_exists = std::filesystem::exists(path_, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "unable to inspect suite database path: " + error.message());
    }
    if (primary_exists && !std::filesystem::is_regular_file(path_, error)) {
        if (error) {
            return fail(ErrorCode::io_error,
                        "unable to inspect suite database path: " + error.message());
        }
        return fail(ErrorCode::io_error,
                    "suite database path is not a regular file");
    }
    error.clear();
    const bool backup_exists = std::filesystem::exists(backup_path, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "unable to inspect suite database backup: " + error.message());
    }
    if (backup_exists && !std::filesystem::is_regular_file(backup_path, error)) {
        if (error) {
            return fail(ErrorCode::io_error,
                        "unable to inspect suite database backup: " + error.message());
        }
        return fail(ErrorCode::io_error,
                    "suite database backup is not a regular file");
    }

    if (!primary_exists && !backup_exists) {
        return SuiteDatabaseSnapshot {};
    }
    if (!primary_exists && backup_exists && !recover_from_backup) {
        return fail(ErrorCode::io_error,
                    "primary suite database is missing and backup recovery is disabled");
    }

    Error primary_error;
    if (primary_exists) {
        auto primary = load_path(path_);
        if (primary) {
            return primary;
        }
        primary_error = primary.error();
        if (!recover_from_backup || !backup_exists) {
            return std::unexpected(primary_error);
        }
    }

    if (backup_exists && recover_from_backup) {
        auto backup = load_path(backup_path);
        if (backup) {
            backup->recovered_from_backup = true;
            std::filesystem::copy_file(
                backup_path, path_,
                std::filesystem::copy_options::overwrite_existing, error);
            if (error) {
                return fail(ErrorCode::io_error,
                            "suite database backup is valid but could not be restored: " +
                                error.message());
            }
            return backup;
        }
        if (!primary_exists) {
            return std::unexpected(backup.error());
        }
    }

    return std::unexpected(primary_error);
}

Result<SuiteDatabaseSnapshot> SuiteDatabase::load_path(
    const std::string& path) const {
    auto bytes = read_file(path);
    if (!bytes) return std::unexpected(bytes.error());
    if (bytes->size() < kMagic.size() + 4U + 8U + 4U + 4U) {
        return fail(ErrorCode::malformed_archive,
                    "suite database is too small");
    }

    const usize payload_size = bytes->size() - 4U;
    const u32 stored_crc = static_cast<u32>((*bytes)[payload_size]) |
                           (static_cast<u32>((*bytes)[payload_size + 1U]) << 8U) |
                           (static_cast<u32>((*bytes)[payload_size + 2U]) << 16U) |
                           (static_cast<u32>((*bytes)[payload_size + 3U]) << 24U);
    if (archive::crc32(std::span<const u8>(bytes->data(), payload_size)) != stored_crc) {
        return fail(ErrorCode::checksum_mismatch,
                    "suite database checksum mismatch");
    }

    Reader reader(std::span<const u8>(bytes->data(), payload_size));
    auto magic = reader.read_bytes(kMagic.size());
    if (!magic) return std::unexpected(magic.error());
    if (!std::equal(magic->begin(), magic->end(), kMagic.begin(), kMagic.end())) {
        return fail(ErrorCode::malformed_archive,
                    "suite database magic does not match");
    }
    auto version = reader.read_u32();
    if (!version) return std::unexpected(version.error());
    if (*version != kFormatVersion) {
        return fail(ErrorCode::unsupported_feature,
                    "suite database format version is not supported");
    }
    auto generation = reader.read_u64();
    auto count = reader.read_u32();
    if (!generation) return std::unexpected(generation.error());
    if (!count) return std::unexpected(count.error());
    if (*count > kMaximumRecords) {
        return fail(ErrorCode::out_of_range,
                    "suite database contains too many records");
    }

    SuiteDatabaseSnapshot snapshot;
    snapshot.generation = *generation;
    snapshot.records.reserve(*count);

    for (u32 record_index = 0; record_index < *count; ++record_index) {
        auto id_value = reader.read_u32();
        if (!id_value) return std::unexpected(id_value.error());
        if (*id_value == 0U || *id_value >
                static_cast<u32>(std::numeric_limits<i32>::max())) {
            return fail(ErrorCode::malformed_archive,
                        "suite database contains an invalid suite ID");
        }

        SuiteDatabaseRecord record;
        record.id = SuiteId {static_cast<i32>(*id_value)};
        auto identity = reader.read_string();
        auto name = reader.read_string();
        auto vendor = reader.read_string();
        auto suite_version = reader.read_string();
        auto jar_path = reader.read_string();
        auto jad_path = reader.read_string();
        if (!identity) return std::unexpected(identity.error());
        if (!name) return std::unexpected(name.error());
        if (!vendor) return std::unexpected(vendor.error());
        if (!suite_version) return std::unexpected(suite_version.error());
        if (!jar_path) return std::unexpected(jar_path.error());
        if (!jad_path) return std::unexpected(jad_path.error());
        record.identity_key = std::move(*identity);
        record.name = std::move(*name);
        record.vendor = std::move(*vendor);
        record.version = std::move(*suite_version);
        record.jar_relative_path = std::move(*jar_path);
        record.jad_relative_path = std::move(*jad_path);

        auto identity_digest = reader.read_bytes(record.identity_sha256.size());
        auto archive_digest = reader.read_bytes(record.archive_sha256.size());
        if (!identity_digest) return std::unexpected(identity_digest.error());
        if (!archive_digest) return std::unexpected(archive_digest.error());
        std::copy(identity_digest->begin(), identity_digest->end(),
                  record.identity_sha256.begin());
        std::copy(archive_digest->begin(), archive_digest->end(),
                  record.archive_sha256.begin());

        auto archive_crc = reader.read_u32();
        auto archive_size = reader.read_u64();
        auto declared_size = reader.read_u64();
        auto flags = reader.read_u8();
        if (!archive_crc) return std::unexpected(archive_crc.error());
        if (!archive_size) return std::unexpected(archive_size.error());
        if (!declared_size) return std::unexpected(declared_size.error());
        if (!flags) return std::unexpected(flags.error());
        if (*flags > 1U) {
            return fail(ErrorCode::malformed_archive,
                        "suite database contains invalid flags");
        }
        record.archive_crc32 = *archive_crc;
        record.archive_size = *archive_size;
        record.declared_jar_size = *declared_size;
        record.has_permission_declarations = *flags != 0U;

        auto midlets = read_strings(reader);
        auto permissions = read_strings(reader);
        if (!midlets) return std::unexpected(midlets.error());
        if (!permissions) return std::unexpected(permissions.error());
        record.midlet_classes = std::move(*midlets);
        record.declared_permissions = std::move(*permissions);

        auto property_count = reader.read_u32();
        if (!property_count) return std::unexpected(property_count.error());
        if (*property_count > kMaximumProperties) {
            return fail(ErrorCode::out_of_range,
                        "suite database contains too many properties");
        }
        record.properties.reserve(*property_count);
        for (u32 property_index = 0; property_index < *property_count;
             ++property_index) {
            auto key = reader.read_string();
            auto value = reader.read_string();
            if (!key) return std::unexpected(key.error());
            if (!value) return std::unexpected(value.error());
            if (!record.properties.emplace(std::move(*key),
                                           std::move(*value)).second) {
                return fail(ErrorCode::malformed_archive,
                            "suite database contains a duplicate property");
            }
        }

        snapshot.records.push_back(std::move(record));
    }

    if (reader.offset() != payload_size) {
        return fail(ErrorCode::malformed_archive,
                    "suite database contains trailing data");
    }
    return snapshot;
}

Result<SuiteDatabaseCommitDurability> SuiteDatabase::commit(
    const SuiteDatabaseSnapshot& snapshot) const {
    if (path_.empty()) {
        return fail(ErrorCode::not_configured,
                    "suite database path is not configured");
    }
    auto bytes = serialize(snapshot);
    if (!bytes) return std::unexpected(bytes.error());

    const std::filesystem::path database_path(path_);
    const std::filesystem::path directory = database_path.parent_path();
    std::error_code error;
    if (!directory.empty()) {
        std::filesystem::create_directories(directory, error);
        if (error) {
            return fail(ErrorCode::io_error,
                        "unable to create suite database directory: " +
                            error.message());
        }
    }

    const std::string temporary_path = path_ + ".tmp";
    const std::string backup_path = path_ + ".bak";
    const int descriptor = ::open(temporary_path.c_str(),
                                  O_WRONLY | O_CREAT | O_TRUNC,
                                  S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "unable to create suite database temporary file: " +
                        std::string(std::strerror(errno)));
    }

    auto written = write_all(descriptor, *bytes);
    if (written && ::fsync(descriptor) != 0) {
        written = fail(ErrorCode::io_error,
                       "unable to sync suite database temporary file: " +
                           std::string(std::strerror(errno)));
    }
    const int close_result = ::close(descriptor);
    if (written && close_result != 0) {
        written = fail(ErrorCode::io_error,
                       "unable to close suite database temporary file");
    }
    if (!written) {
        std::filesystem::remove(temporary_path, error);
        return std::unexpected(written.error());
    }

    error.clear();
    std::filesystem::remove(backup_path, error);
    error.clear();
    const bool primary_exists = std::filesystem::exists(path_, error);
    if (error) {
        std::filesystem::remove(temporary_path, error);
        return fail(ErrorCode::io_error,
                    "unable to inspect previous suite database: " +
                        error.message());
    }
    if (primary_exists) {
        std::filesystem::rename(path_, backup_path, error);
        if (error) {
            std::filesystem::remove(temporary_path, error);
            return fail(ErrorCode::io_error,
                        "unable to rotate suite database backup: " +
                            error.message());
        }
    }

    error.clear();
    std::filesystem::rename(temporary_path, path_, error);
    if (error) {
        std::error_code restore_error;
        if (primary_exists) {
            std::filesystem::rename(backup_path, path_, restore_error);
        }
        std::filesystem::remove(temporary_path, restore_error);
        return fail(ErrorCode::io_error,
                    "unable to commit suite database atomically: " +
                        error.message());
    }

    Status directory_sync;
    if (fault_injector_) {
        directory_sync = fault_injector_(
            SuiteDatabaseFaultPoint::before_primary_directory_sync);
    }
    if (directory_sync && !directory.empty()) {
        directory_sync = fsync_directory(directory);
    }
    if (!directory_sync) {
        // The new primary is visible in this process, but its directory entry
        // may not survive a crash. Keep the previous generation in .bak and
        // tell the caller to preserve both suite-file transaction candidates.
        return SuiteDatabaseCommitDurability::unknown;
    }

    // Keep a checksum-valid mirror of the committed generation. This is
    // best-effort because the primary database is already durable; failure to
    // refresh the mirror must not turn a successful atomic commit into a
    // rollback request after the caller has activated matching suite files.
    error.clear();
    std::filesystem::copy_file(
        path_, backup_path,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (!error) {
        const int backup_descriptor = ::open(backup_path.c_str(), O_RDONLY);
        if (backup_descriptor >= 0) {
            static_cast<void>(::fsync(backup_descriptor));
            static_cast<void>(::close(backup_descriptor));
        }
        if (!directory.empty()) {
            static_cast<void>(fsync_directory(directory));
        }
    }
    return SuiteDatabaseCommitDurability::durable;
}

} // namespace phoneme::runtime
