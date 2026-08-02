#include "phoneme/runtime/RecordStoreRegistry.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <limits>
#include <set>
#include <unordered_set>
#include <utility>

#include <zlib.h>

namespace phoneme::runtime {
namespace {

constexpr std::array<u8, 4> kMagic {'P', 'M', 'R', 'S'};
constexpr u32 kLegacyFormatVersion = 1;
constexpr u32 kCurrentFormatVersion = 2;
constexpr u32 kSupportedFeatureFlags = 0;
constexpr usize kMaximumStoreNameBytes = 128;

void append_u32(std::vector<u8>& output, u32 value) {
    output.push_back(static_cast<u8>(value >> 24U));
    output.push_back(static_cast<u8>(value >> 16U));
    output.push_back(static_cast<u8>(value >> 8U));
    output.push_back(static_cast<u8>(value));
}

void append_u64(std::vector<u8>& output, u64 value) {
    append_u32(output, static_cast<u32>(value >> 32U));
    append_u32(output, static_cast<u32>(value));
}

[[nodiscard]] Result<u32> read_u32(std::span<const u8> input,
                                   usize& cursor) {
    if (cursor > input.size() || input.size() - cursor < 4U) {
        return fail(ErrorCode::malformed_archive,
                    "RMS file is truncated");
    }
    const u32 value = (static_cast<u32>(input[cursor]) << 24U) |
                      (static_cast<u32>(input[cursor + 1U]) << 16U) |
                      (static_cast<u32>(input[cursor + 2U]) << 8U) |
                      static_cast<u32>(input[cursor + 3U]);
    cursor += 4U;
    return value;
}

[[nodiscard]] Result<u64> read_u64(std::span<const u8> input,
                                   usize& cursor) {
    auto high = read_u32(input, cursor);
    auto low = read_u32(input, cursor);
    if (!high) return std::unexpected(high.error());
    if (!low) return std::unexpected(low.error());
    return (static_cast<u64>(*high) << 32U) | *low;
}

[[nodiscard]] Status write_all(int descriptor,
                               std::span<const u8> bytes) {
    usize offset = 0;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(descriptor,
                                        bytes.data() + offset,
                                        bytes.size() - offset);
        if (written < 0) {
            if (errno == EINTR) continue;
            return fail(ErrorCode::io_error,
                        "failed to write RMS file");
        }
        if (written == 0) {
            return fail(ErrorCode::io_error,
                        "RMS file write made no progress");
        }
        offset += static_cast<usize>(written);
    }
    return {};
}

[[nodiscard]] Result<std::vector<u8>> read_all_file(
    const std::string& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return fail(errno == ENOENT ? ErrorCode::class_not_found
                                    : ErrorCode::io_error,
                    "failed to open RMS file");
    }
    struct stat status {};
    if (::fstat(descriptor, &status) != 0 || status.st_size < 0) {
        ::close(descriptor);
        return fail(ErrorCode::io_error,
                    "failed to inspect RMS file");
    }
    if (static_cast<u64>(status.st_size) >
        static_cast<u64>(std::numeric_limits<usize>::max())) {
        ::close(descriptor);
        return fail(ErrorCode::overflow,
                    "RMS file is too large");
    }
    std::vector<u8> bytes(static_cast<usize>(status.st_size));
    usize offset = 0;
    while (offset < bytes.size()) {
        const ssize_t count = ::read(descriptor,
                                     bytes.data() + offset,
                                     bytes.size() - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            ::close(descriptor);
            return fail(ErrorCode::io_error,
                        "failed to read RMS file");
        }
        if (count == 0) break;
        offset += static_cast<usize>(count);
    }
    ::close(descriptor);
    if (offset != bytes.size()) {
        return fail(ErrorCode::io_error,
                    "RMS file ended before its declared size");
    }
    return bytes;
}

[[nodiscard]] std::string hexadecimal_u64(u64 value) {
    constexpr std::array<char, 16> digits {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    std::string result(16, '0');
    for (usize index = 0; index < result.size(); ++index) {
        const usize shift = (result.size() - 1U - index) * 4U;
        result[index] = digits[static_cast<usize>((value >> shift) & 0xFU)];
    }
    return result;
}

[[nodiscard]] u64 fnv1a(std::string_view value) noexcept {
    u64 hash = 1469598103934665603ULL;
    for (const char character : value) {
        hash ^= static_cast<u8>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] i64 next_modified_time(i64 previous) noexcept {
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (previous == std::numeric_limits<i64>::max()) return previous;
    return std::max(now, previous + 1);
}

[[nodiscard]] bool is_store_candidate(const std::string& path,
                                      std::string& canonical_path) {
    if (path.ends_with(".rms")) {
        canonical_path = path;
        return true;
    }
    if (path.ends_with(".rms.tmp") || path.ends_with(".rms.bak")) {
        canonical_path = path.substr(0, path.size() - 4U);
        return true;
    }
    return false;
}

} // namespace

Status RecordStoreRegistry::configure(std::string root_directory) {
    if (root_directory.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "RMS root directory must not be empty");
    }
    std::error_code error;
    std::filesystem::create_directories(root_directory, error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "failed to create RMS root directory: " +
                        error.message());
    }
    const auto canonical = std::filesystem::weakly_canonical(
        std::filesystem::absolute(root_directory, error), error);
    if (error) {
        return fail(ErrorCode::io_error,
                    "failed to canonicalize RMS root directory: " +
                        error.message());
    }

    std::scoped_lock lock(mutex_);
    for (const auto& [name, store] : stores_) {
        (void)name;
        if (store.open_count != 0) {
            return fail(ErrorCode::already_running,
                        "cannot reconfigure RMS while stores are open");
        }
    }
    root_directory_ = canonical.string();
    stores_.clear();
    return {};
}

Status RecordStoreRegistry::validate_name(std::string_view name) {
    if (name.empty() || name.size() > kMaximumStoreNameBytes) {
        return fail(ErrorCode::invalid_argument,
                    "RMS store name is empty or too long");
    }
    if (name.find('\0') != std::string_view::npos) {
        return fail(ErrorCode::invalid_argument,
                    "RMS store name contains NUL");
    }
    return {};
}

Result<RecordStoreRegistry::Store*> RecordStoreRegistry::load_unlocked(
    std::string_view name,
    bool create) {
    if (root_directory_.empty()) {
        return fail(ErrorCode::not_configured,
                    "RMS registry is not configured");
    }
    auto validated = validate_name(name);
    if (!validated) return std::unexpected(validated.error());
    const auto existing = stores_.find(std::string(name));
    if (existing != stores_.end()) return &existing->second;

    const std::string path = path_for_name(name);
    auto loaded = recover_file_unlocked(path);
    if (loaded) {
        if (loaded->name != name) {
            return fail(ErrorCode::malformed_archive,
                        "RMS file hash collision or name mismatch");
        }
        const auto [iterator, inserted] = stores_.emplace(
            loaded->name, std::move(*loaded));
        (void)inserted;
        return &iterator->second;
    }
    if (loaded.error().code != ErrorCode::class_not_found) {
        return std::unexpected(loaded.error());
    }
    if (!create) {
        return fail(ErrorCode::class_not_found,
                    "RMS store does not exist");
    }
    Store store;
    store.name = std::string(name);
    store.storage_format = kCurrentFormatVersion;
    store.version = 0;
    store.next_record_id = 1;
    store.last_modified_ms = current_time_millis();
    const auto [iterator, inserted] = stores_.emplace(store.name,
                                                     std::move(store));
    (void)inserted;
    auto persisted = persist_unlocked(iterator->second);
    if (!persisted) {
        stores_.erase(iterator);
        return std::unexpected(persisted.error());
    }
    return &iterator->second;
}

Result<RecordStoreRegistry::Store*> RecordStoreRegistry::require_open_unlocked(
    std::string_view name) {
    auto loaded = load_unlocked(name, false);
    if (!loaded) return std::unexpected(loaded.error());
    if ((*loaded)->open_count == 0) {
        return fail(ErrorCode::invalid_state,
                    "RMS store is not open");
    }
    return *loaded;
}

Status RecordStoreRegistry::open(std::string_view name, bool create) {
    std::scoped_lock lock(mutex_);
    auto store = load_unlocked(name, create);
    if (!store) return std::unexpected(store.error());
    if ((*store)->open_count == std::numeric_limits<usize>::max()) {
        return fail(ErrorCode::overflow,
                    "RMS open handle count is exhausted");
    }
    ++(*store)->open_count;
    return {};
}

Status RecordStoreRegistry::close(std::string_view name) {
    std::scoped_lock lock(mutex_);
    auto store = load_unlocked(name, false);
    if (!store) return std::unexpected(store.error());
    if ((*store)->open_count == 0) {
        return fail(ErrorCode::invalid_state,
                    "RMS store is already closed");
    }
    --(*store)->open_count;
    return {};
}

Status RecordStoreRegistry::delete_store(std::string_view name) {
    std::scoped_lock lock(mutex_);
    auto store = load_unlocked(name, false);
    if (!store) return std::unexpected(store.error());
    if ((*store)->open_count != 0) {
        return fail(ErrorCode::already_running,
                    "cannot delete an open RMS store");
    }

    const std::string path = path_for_name(name);
    bool removed = false;
    for (const std::string& suffix : {std::string {},
                                      std::string {".tmp"},
                                      std::string {".bak"}}) {
        if (::unlink((path + suffix).c_str()) == 0) {
            removed = true;
        } else if (errno != ENOENT) {
            return fail(ErrorCode::io_error,
                        "failed to delete RMS store");
        }
    }
    if (!removed) {
        return fail(ErrorCode::class_not_found,
                    "RMS store does not exist");
    }
    auto synchronized = sync_directory_unlocked();
    if (!synchronized) return synchronized;
    stores_.erase(std::string(name));
    return {};
}

Result<std::vector<std::string>> RecordStoreRegistry::list_store_names() {
    std::scoped_lock lock(mutex_);
    if (root_directory_.empty()) {
        return fail(ErrorCode::not_configured,
                    "RMS registry is not configured");
    }
    std::set<std::string> names;
    for (const auto& [name, store] : stores_) {
        (void)store;
        names.insert(name);
    }

    std::set<std::string> canonical_paths;
    std::error_code error;
    std::filesystem::directory_iterator iterator(root_directory_, error);
    const std::filesystem::directory_iterator end;
    if (error) {
        return fail(ErrorCode::io_error,
                    "failed to list RMS directory: " + error.message());
    }
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            return fail(ErrorCode::io_error,
                        "failed while listing RMS directory: " +
                            error.message());
        }
        if (!iterator->is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        std::string canonical_path;
        if (is_store_candidate(iterator->path().string(), canonical_path)) {
            canonical_paths.insert(std::move(canonical_path));
        }
    }
    for (const auto& path : canonical_paths) {
        auto store = recover_file_unlocked(path);
        if (!store) return std::unexpected(store.error());
        names.insert(store->name);
    }
    return std::vector<std::string>(names.begin(), names.end());
}

Result<RecordStoreInfo> RecordStoreRegistry::info(std::string_view name) {
    std::scoped_lock lock(mutex_);
    auto store = require_open_unlocked(name);
    if (!store) return std::unexpected(store.error());
    return RecordStoreInfo {
        .name = (*store)->name,
        .version = (*store)->version,
        .next_record_id = (*store)->next_record_id,
        .last_modified_ms = (*store)->last_modified_ms,
        .total_record_bytes = used_bytes_unlocked(**store),
        .record_count = (*store)->records.size(),
    };
}

Result<i32> RecordStoreRegistry::add_record(
    std::string_view name,
    std::span<const u8> bytes) {
    std::scoped_lock lock(mutex_);
    auto store = require_open_unlocked(name);
    if (!store) return std::unexpected(store.error());
    if ((*store)->next_record_id <= 0 ||
        (*store)->next_record_id >= std::numeric_limits<i32>::max()) {
        return fail(ErrorCode::overflow,
                    "RMS record ID space is exhausted");
    }
    if ((*store)->version == std::numeric_limits<i32>::max()) {
        return fail(ErrorCode::overflow,
                    "RMS store version is exhausted");
    }
    auto suite_used = suite_used_bytes_unlocked();
    if (!suite_used) return std::unexpected(suite_used.error());
    if (*suite_used > quota_bytes_ ||
        bytes.size() > quota_bytes_ - *suite_used) {
        return fail(ErrorCode::overflow,
                    "RMS suite quota is exceeded");
    }

    Store backup = **store;
    const i32 id = (*store)->next_record_id++;
    (*store)->records.emplace(id,
                              std::vector<u8>(bytes.begin(), bytes.end()));
    ++(*store)->version;
    (*store)->last_modified_ms = next_modified_time(
        (*store)->last_modified_ms);
    (*store)->storage_format = kCurrentFormatVersion;
    auto persisted = persist_unlocked(**store);
    if (!persisted) {
        **store = std::move(backup);
        return std::unexpected(persisted.error());
    }
    return id;
}

Status RecordStoreRegistry::set_record(std::string_view name,
                                       i32 record_id,
                                       std::span<const u8> bytes) {
    std::scoped_lock lock(mutex_);
    auto store = require_open_unlocked(name);
    if (!store) return std::unexpected(store.error());
    const auto record = (*store)->records.find(record_id);
    if (record == (*store)->records.end()) {
        return fail(ErrorCode::out_of_range,
                    "RMS record ID does not exist");
    }
    if ((*store)->version == std::numeric_limits<i32>::max()) {
        return fail(ErrorCode::overflow,
                    "RMS store version is exhausted");
    }
    if (bytes.size() > record->second.size()) {
        auto suite_used = suite_used_bytes_unlocked();
        if (!suite_used) return std::unexpected(suite_used.error());
        const usize growth = bytes.size() - record->second.size();
        if (*suite_used > quota_bytes_ || growth > quota_bytes_ - *suite_used) {
            return fail(ErrorCode::overflow,
                        "RMS suite quota is exceeded");
        }
    }

    Store backup = **store;
    record->second.assign(bytes.begin(), bytes.end());
    ++(*store)->version;
    (*store)->last_modified_ms = next_modified_time(
        (*store)->last_modified_ms);
    (*store)->storage_format = kCurrentFormatVersion;
    auto persisted = persist_unlocked(**store);
    if (!persisted) {
        **store = std::move(backup);
        return persisted;
    }
    return {};
}

Status RecordStoreRegistry::delete_record(std::string_view name,
                                          i32 record_id) {
    std::scoped_lock lock(mutex_);
    auto store = require_open_unlocked(name);
    if (!store) return std::unexpected(store.error());
    const auto record = (*store)->records.find(record_id);
    if (record == (*store)->records.end()) {
        return fail(ErrorCode::out_of_range,
                    "RMS record ID does not exist");
    }
    if ((*store)->version == std::numeric_limits<i32>::max()) {
        return fail(ErrorCode::overflow,
                    "RMS store version is exhausted");
    }

    Store backup = **store;
    (*store)->records.erase(record);
    ++(*store)->version;
    (*store)->last_modified_ms = next_modified_time(
        (*store)->last_modified_ms);
    (*store)->storage_format = kCurrentFormatVersion;
    auto persisted = persist_unlocked(**store);
    if (!persisted) {
        **store = std::move(backup);
        return persisted;
    }
    return {};
}

Result<std::vector<u8>> RecordStoreRegistry::record(
    std::string_view name,
    i32 record_id) {
    std::scoped_lock lock(mutex_);
    auto store = require_open_unlocked(name);
    if (!store) return std::unexpected(store.error());
    const auto record = (*store)->records.find(record_id);
    if (record == (*store)->records.end()) {
        return fail(ErrorCode::out_of_range,
                    "RMS record ID does not exist");
    }
    return record->second;
}

Result<std::vector<RecordSnapshot>> RecordStoreRegistry::snapshot(
    std::string_view name) {
    std::scoped_lock lock(mutex_);
    auto store = require_open_unlocked(name);
    if (!store) return std::unexpected(store.error());
    std::vector<RecordSnapshot> result;
    result.reserve((*store)->records.size());
    for (const auto& [id, bytes] : (*store)->records) {
        result.push_back(RecordSnapshot {.id = id, .bytes = bytes});
    }
    return result;
}

Result<usize> RecordStoreRegistry::available_bytes(std::string_view name) {
    std::scoped_lock lock(mutex_);
    auto store = require_open_unlocked(name);
    if (!store) return std::unexpected(store.error());
    (void)store;
    auto used = suite_used_bytes_unlocked();
    if (!used) return std::unexpected(used.error());
    return *used >= quota_bytes_ ? 0U : quota_bytes_ - *used;
}

Result<RecordStoreRegistry::Store> RecordStoreRegistry::read_file_unlocked(
    const std::string& path) const {
    auto bytes = read_all_file(path);
    if (!bytes) return std::unexpected(bytes.error());
    if (bytes->size() < 16U ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes->begin())) {
        return fail(ErrorCode::malformed_archive,
                    "RMS file has an invalid header");
    }
    usize cursor = 4U;
    auto format = read_u32(*bytes, cursor);
    auto payload_size = read_u32(*bytes, cursor);
    auto expected_crc = read_u32(*bytes, cursor);
    if (!format || !payload_size || !expected_crc) {
        return fail(ErrorCode::malformed_archive,
                    "RMS file header is truncated");
    }
    if ((*format != kLegacyFormatVersion &&
         *format != kCurrentFormatVersion) ||
        *payload_size != bytes->size() - cursor) {
        return fail(ErrorCode::unsupported_archive,
                    "RMS file version or size is invalid");
    }
    const uLong actual_crc = ::crc32(
        ::crc32(0L, Z_NULL, 0), bytes->data() + cursor,
        static_cast<uInt>(*payload_size));
    if (static_cast<u32>(actual_crc) != *expected_crc) {
        return fail(ErrorCode::checksum_mismatch,
                    "RMS file checksum does not match");
    }

    const std::span<const u8> payload(bytes->data() + cursor,
                                      *payload_size);
    usize payload_cursor = 0;
    auto name_length = read_u32(payload, payload_cursor);
    if (!name_length || *name_length > kMaximumStoreNameBytes ||
        *name_length > payload.size() - payload_cursor) {
        return fail(ErrorCode::malformed_archive,
                    "RMS store name is truncated or invalid");
    }
    Store store;
    store.storage_format = *format;
    store.name.assign(
        reinterpret_cast<const char*>(payload.data() + payload_cursor),
        *name_length);
    payload_cursor += *name_length;
    auto validated = validate_name(store.name);
    if (!validated) {
        return fail(ErrorCode::malformed_archive,
                    "RMS store name is invalid");
    }

    auto version = read_u32(payload, payload_cursor);
    auto next_id = read_u32(payload, payload_cursor);
    auto modified = read_u64(payload, payload_cursor);
    if (!version || !next_id || !modified) {
        return fail(ErrorCode::malformed_archive,
                    "RMS metadata is truncated");
    }
    if (*version > static_cast<u32>(std::numeric_limits<i32>::max()) ||
        *next_id == 0U ||
        *next_id > static_cast<u32>(std::numeric_limits<i32>::max()) ||
        *modified > static_cast<u64>(std::numeric_limits<i64>::max())) {
        return fail(ErrorCode::malformed_archive,
                    "RMS metadata is outside supported ranges");
    }
    store.version = static_cast<i32>(*version);
    store.next_record_id = static_cast<i32>(*next_id);
    store.last_modified_ms = static_cast<i64>(*modified);

    if (*format == kCurrentFormatVersion) {
        auto feature_flags = read_u32(payload, payload_cursor);
        if (!feature_flags ||
            (*feature_flags & ~kSupportedFeatureFlags) != 0U) {
            return fail(ErrorCode::unsupported_archive,
                        "RMS file uses unsupported feature flags");
        }
    }
    auto record_count = read_u32(payload, payload_cursor);
    if (!record_count ||
        static_cast<usize>(*record_count) >
            (payload.size() - payload_cursor) / 8U) {
        return fail(ErrorCode::malformed_archive,
                    "RMS record count is invalid");
    }

    usize total_record_bytes = 0;
    i32 maximum_record_id = 0;
    for (u32 index = 0; index < *record_count; ++index) {
        auto id = read_u32(payload, payload_cursor);
        auto length = read_u32(payload, payload_cursor);
        if (!id || !length || *id == 0U ||
            *id > static_cast<u32>(std::numeric_limits<i32>::max()) ||
            *length > payload.size() - payload_cursor) {
            return fail(ErrorCode::malformed_archive,
                        "RMS record entry is invalid");
        }
        if (static_cast<usize>(*length) >
            quota_bytes_ - std::min(quota_bytes_, total_record_bytes)) {
            return fail(ErrorCode::overflow,
                        "RMS store exceeds the configured quota");
        }
        total_record_bytes += static_cast<usize>(*length);
        std::vector<u8> record(payload.begin() +
                                   static_cast<std::ptrdiff_t>(payload_cursor),
                               payload.begin() + static_cast<std::ptrdiff_t>(
                                   payload_cursor + *length));
        payload_cursor += *length;
        const i32 record_id = static_cast<i32>(*id);
        maximum_record_id = std::max(maximum_record_id, record_id);
        if (!store.records.emplace(record_id, std::move(record)).second) {
            return fail(ErrorCode::malformed_archive,
                        "RMS file contains duplicate record IDs");
        }
    }
    if (payload_cursor != payload.size()) {
        return fail(ErrorCode::malformed_archive,
                    "RMS file contains trailing data");
    }
    if (maximum_record_id >= store.next_record_id) {
        return fail(ErrorCode::malformed_archive,
                    "RMS next record ID does not follow persisted records");
    }
    return store;
}

Result<RecordStoreRegistry::Store> RecordStoreRegistry::recover_file_unlocked(
    const std::string& canonical_path) const {
    struct Candidate final {
        Store store;
        std::string path;
        usize priority {0};
    };

    std::optional<Candidate> best;
    std::optional<Error> first_error;
    bool found_file = false;
    const std::array<std::string, 3> paths {
        canonical_path,
        canonical_path + ".tmp",
        canonical_path + ".bak",
    };
    for (usize priority = 0; priority < paths.size(); ++priority) {
        std::error_code exists_error;
        if (!std::filesystem::exists(paths[priority], exists_error)) {
            if (exists_error && !first_error.has_value()) {
                first_error = Error::make(
                    ErrorCode::io_error,
                    "failed to inspect RMS recovery candidate: " +
                        exists_error.message());
            }
            continue;
        }
        found_file = true;
        auto candidate = read_file_unlocked(paths[priority]);
        if (!candidate) {
            if (!first_error.has_value() || priority == 0U) {
                first_error = candidate.error();
            }
            continue;
        }
        if (path_for_name(candidate->name) != canonical_path) {
            if (!first_error.has_value() || priority == 0U) {
                first_error = Error::make(
                    ErrorCode::malformed_archive,
                    "RMS recovery candidate has a mismatched store hash");
            }
            continue;
        }
        const bool newer = !best.has_value() ||
            candidate->version > best->store.version ||
            (candidate->version == best->store.version &&
             candidate->last_modified_ms > best->store.last_modified_ms) ||
            (candidate->version == best->store.version &&
             candidate->last_modified_ms == best->store.last_modified_ms &&
             priority < best->priority);
        if (newer) {
            best = Candidate {
                .store = std::move(*candidate),
                .path = paths[priority],
                .priority = priority,
            };
        }
    }

    if (!best.has_value()) {
        if (first_error.has_value()) return std::unexpected(*first_error);
        return fail(found_file ? ErrorCode::malformed_archive
                               : ErrorCode::class_not_found,
                    found_file ? "no valid RMS recovery candidate exists"
                               : "RMS store does not exist");
    }

    const bool needs_restore = best->path != canonical_path ||
        best->store.storage_format != kCurrentFormatVersion;
    if (needs_restore) {
        auto restored = persist_unlocked(best->store);
        if (!restored) return std::unexpected(restored.error());
        best->store.storage_format = kCurrentFormatVersion;
    } else {
        bool removed = false;
        for (const std::string& suffix : {std::string {".tmp"},
                                          std::string {".bak"}}) {
            if (::unlink((canonical_path + suffix).c_str()) == 0) {
                removed = true;
            } else if (errno != ENOENT) {
                return fail(ErrorCode::io_error,
                            "failed to remove stale RMS recovery file");
            }
        }
        if (removed) {
            auto synchronized = sync_directory_unlocked();
            if (!synchronized) return std::unexpected(synchronized.error());
        }
    }
    return std::move(best->store);
}

Status RecordStoreRegistry::persist_unlocked(const Store& store) const {
    std::vector<u8> payload;
    const usize estimated = 4U + store.name.size() + 24U +
        used_bytes_unlocked(store) + store.records.size() * 8U;
    payload.reserve(estimated);
    append_u32(payload, static_cast<u32>(store.name.size()));
    for (const char character : store.name) {
        payload.push_back(static_cast<u8>(
            static_cast<unsigned char>(character)));
    }
    append_u32(payload, static_cast<u32>(store.version));
    append_u32(payload, static_cast<u32>(store.next_record_id));
    append_u64(payload, static_cast<u64>(store.last_modified_ms));
    append_u32(payload, kSupportedFeatureFlags);
    append_u32(payload, static_cast<u32>(store.records.size()));
    for (const auto& [id, bytes] : store.records) {
        if (id <= 0 || bytes.size() > std::numeric_limits<u32>::max()) {
            return fail(ErrorCode::overflow,
                        "RMS record is too large or has an invalid ID");
        }
        append_u32(payload, static_cast<u32>(id));
        append_u32(payload, static_cast<u32>(bytes.size()));
        payload.insert(payload.end(), bytes.begin(), bytes.end());
    }
    if (payload.size() > std::numeric_limits<u32>::max()) {
        return fail(ErrorCode::overflow,
                    "RMS store is too large to serialize");
    }
    const uLong checksum = ::crc32(
        ::crc32(0L, Z_NULL, 0), payload.data(),
        static_cast<uInt>(payload.size()));
    std::vector<u8> file;
    file.reserve(16U + payload.size());
    file.insert(file.end(), kMagic.begin(), kMagic.end());
    append_u32(file, kCurrentFormatVersion);
    append_u32(file, static_cast<u32>(payload.size()));
    append_u32(file, static_cast<u32>(checksum));
    file.insert(file.end(), payload.begin(), payload.end());

    const std::string path = path_for_name(store.name);
    const std::string temporary = path + ".tmp";
    const std::string backup = path + ".bak";
    const int descriptor = ::open(temporary.c_str(),
                                  O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                                  S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "failed to create temporary RMS file");
    }
    auto written = write_all(descriptor, file);
    if (!written) {
        ::close(descriptor);
        ::unlink(temporary.c_str());
        return written;
    }
    if (::fsync(descriptor) != 0) {
        ::close(descriptor);
        ::unlink(temporary.c_str());
        return fail(ErrorCode::io_error,
                    "failed to synchronize RMS file");
    }
    if (::close(descriptor) != 0) {
        ::unlink(temporary.c_str());
        return fail(ErrorCode::io_error,
                    "failed to close RMS file");
    }

    (void)::unlink(backup.c_str());
    bool has_backup = false;
    if (::link(path.c_str(), backup.c_str()) == 0) {
        has_backup = true;
    } else if (errno != ENOENT) {
        ::unlink(temporary.c_str());
        return fail(ErrorCode::io_error,
                    "failed to preserve the previous RMS generation");
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        if (has_backup) (void)::unlink(backup.c_str());
        return fail(ErrorCode::io_error,
                    "failed to atomically replace RMS file");
    }
    auto synchronized = sync_directory_unlocked();
    if (!synchronized) {
        if (has_backup) {
            (void)::rename(backup.c_str(), path.c_str());
        } else {
            (void)::unlink(path.c_str());
        }
        (void)sync_directory_unlocked();
        return synchronized;
    }
    if (has_backup) {
        (void)::unlink(backup.c_str());
        (void)sync_directory_unlocked();
    }
    return {};
}

Status RecordStoreRegistry::sync_directory_unlocked() const {
    const int descriptor = ::open(root_directory_.c_str(),
                                  O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "failed to open RMS directory for synchronization");
    }
    if (::fsync(descriptor) != 0) {
        const int saved_errno = errno;
        ::close(descriptor);
        if (saved_errno == EINVAL || saved_errno == ENOTSUP) {
            return {};
        }
        return fail(ErrorCode::io_error,
                    "failed to synchronize RMS directory");
    }
    if (::close(descriptor) != 0) {
        return fail(ErrorCode::io_error,
                    "failed to close RMS directory");
    }
    return {};
}

Result<usize> RecordStoreRegistry::suite_used_bytes_unlocked() const {
    usize total = 0;
    std::unordered_set<std::string> loaded_names;
    loaded_names.reserve(stores_.size());
    for (const auto& [name, store] : stores_) {
        const usize used = used_bytes_unlocked(store);
        if (used > std::numeric_limits<usize>::max() - total) {
            return fail(ErrorCode::overflow,
                        "RMS suite size overflowed");
        }
        total += used;
        loaded_names.insert(name);
    }

    std::set<std::string> canonical_paths;
    std::error_code error;
    std::filesystem::directory_iterator iterator(root_directory_, error);
    const std::filesystem::directory_iterator end;
    if (error) {
        return fail(ErrorCode::io_error,
                    "failed to inspect RMS suite quota: " + error.message());
    }
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            return fail(ErrorCode::io_error,
                        "failed while inspecting RMS suite quota: " +
                            error.message());
        }
        if (!iterator->is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        std::string canonical_path;
        if (is_store_candidate(iterator->path().string(), canonical_path)) {
            canonical_paths.insert(std::move(canonical_path));
        }
    }
    for (const auto& path : canonical_paths) {
        auto store = recover_file_unlocked(path);
        if (!store) return std::unexpected(store.error());
        if (loaded_names.contains(store->name)) continue;
        const usize used = used_bytes_unlocked(*store);
        if (used > std::numeric_limits<usize>::max() - total) {
            return fail(ErrorCode::overflow,
                        "RMS suite size overflowed");
        }
        total += used;
    }
    return total;
}

std::string RecordStoreRegistry::path_for_name(
    std::string_view name) const {
    return root_directory_ + "/" + hexadecimal_u64(fnv1a(name)) + ".rms";
}

usize RecordStoreRegistry::used_bytes_unlocked(
    const Store& store) const noexcept {
    usize total = 0;
    for (const auto& [id, bytes] : store.records) {
        (void)id;
        if (bytes.size() > std::numeric_limits<usize>::max() - total) {
            return std::numeric_limits<usize>::max();
        }
        total += bytes.size();
    }
    return total;
}

i64 RecordStoreRegistry::current_time_millis() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

} // namespace phoneme::runtime
