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

[[nodiscard]] constexpr std::array<u8, 4> encode_u32(u32 value) noexcept {
    return {
        static_cast<u8>(value >> 24U),
        static_cast<u8>(value >> 16U),
        static_cast<u8>(value >> 8U),
        static_cast<u8>(value),
    };
}

[[nodiscard]] constexpr std::array<u8, 8> encode_u64(u64 value) noexcept {
    const auto high = encode_u32(static_cast<u32>(value >> 32U));
    const auto low = encode_u32(static_cast<u32>(value));
    return {
        high[0], high[1], high[2], high[3],
        low[0], low[1], low[2], low[3],
    };
}

[[nodiscard]] std::string base64url_encode(std::string_view value) {
    static constexpr std::array<char, 64> alphabet {
        'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
        'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
        'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
        'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
        'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
        'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
        'w', 'x', 'y', 'z', '0', '1', '2', '3',
        '4', '5', '6', '7', '8', '9', '-', '_',
    };
    std::string result;
    result.reserve((value.size() * 4U + 2U) / 3U);
    u32 accumulator = 0;
    usize bits = 0;
    for (const char character : value) {
        accumulator = (accumulator << 8U) |
            static_cast<u8>(static_cast<unsigned char>(character));
        bits += 8U;
        while (bits >= 6U) {
            bits -= 6U;
            result.push_back(alphabet[static_cast<usize>(
                (accumulator >> bits) & 0x3FU)]);
        }
    }
    if (bits != 0U) {
        result.push_back(alphabet[static_cast<usize>(
            (accumulator << (6U - bits)) & 0x3FU)]);
    }
    return result;
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

[[nodiscard]] bool is_delete_tombstone_candidate(
    const std::filesystem::path& path) {
    const std::string filename = path.filename().string();
    const usize marker = filename.find(".delete-");
    if (marker == std::string::npos || marker + 8U >= filename.size()) {
        return false;
    }
    std::string ignored;
    return is_store_candidate(filename.substr(0, marker), ignored);
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
    const std::string previous_root = root_directory_;
    root_directory_ = canonical.string();
    auto scavenged = scavenge_delete_tombstones_unlocked();
    if (!scavenged) {
        root_directory_ = previous_root;
        return scavenged;
    }
    stores_.clear();
    mutation_generation_.store(0, std::memory_order_relaxed);
    return {};
}

void RecordStoreRegistry::set_fault_injector(
    RecordStoreFaultInjector injector) {
    std::scoped_lock lock(mutex_);
    fault_injector_ = std::move(injector);
}

void RecordStoreRegistry::clear_fault_injector() {
    std::scoped_lock lock(mutex_);
    fault_injector_ = {};
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
    auto loaded = recover_file_unlocked(path, name);
    if (!loaded && loaded.error().code == ErrorCode::class_not_found) {
        const std::string legacy_path = legacy_path_for_name(name);
        if (legacy_path != path) {
            auto legacy = recover_file_unlocked(legacy_path, name);
            if (legacy) {
                auto migrated = migrate_legacy_path_unlocked(
                    *legacy, legacy_path);
                if (!migrated) return std::unexpected(migrated.error());
                loaded = std::move(legacy);
            } else if (legacy.error().code != ErrorCode::class_not_found) {
                return std::unexpected(legacy.error());
            }
        }
    }
    if (loaded) {
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
    if ((*store)->open_count == 0U && (*store)->pending_persist) {
        auto persisted = persist_unlocked(**store);
        if (!persisted) return persisted;
        (*store)->pending_persist = false;
    }
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

    struct MovedPath final {
        std::string original;
        std::string tombstone;
    };
    std::vector<MovedPath> moved;
    moved.reserve(6U);
    const std::string tombstone_suffix =
        ".delete-" + std::to_string(static_cast<u64>(::getpid()));

    const auto rollback = [&](const Error& original) -> Status {
        bool restored = true;
        for (auto iterator = moved.rbegin(); iterator != moved.rend();
             ++iterator) {
            if (::rename(iterator->tombstone.c_str(),
                         iterator->original.c_str()) != 0) {
                restored = false;
            }
        }
        auto synchronized = sync_directory_unlocked(false);
        if (!restored || !synchronized) {
            return fail(ErrorCode::io_error,
                        "failed to roll back RMS store deletion");
        }
        return std::unexpected(original);
    };

    const auto stage_family = [&](const std::string& canonical_path)
        -> Status {
        for (const std::string& suffix : {std::string {},
                                          std::string {".tmp"},
                                          std::string {".bak"}}) {
            const std::string original = canonical_path + suffix;
            const std::string tombstone = original + tombstone_suffix;
            if (::unlink(tombstone.c_str()) != 0 && errno != ENOENT) {
                return rollback(Error::make(
                    ErrorCode::io_error,
                    "failed to clear stale RMS delete tombstone"));
            }
            if (::rename(original.c_str(), tombstone.c_str()) == 0) {
                moved.push_back(MovedPath {
                    .original = original,
                    .tombstone = tombstone,
                });
            } else if (errno != ENOENT) {
                return rollback(Error::make(
                    ErrorCode::io_error,
                    "failed to stage RMS store deletion"));
            }
        }
        return {};
    };

    const std::string path = path_for_name(name);
    auto staged = stage_family(path);
    if (!staged) return staged;
    const std::string legacy_path = legacy_path_for_name(name);
    if (legacy_path != path) {
        staged = stage_family(legacy_path);
        if (!staged) return staged;
    }
    if (moved.empty()) {
        return fail(ErrorCode::class_not_found,
                    "RMS store does not exist");
    }

    auto synchronized = sync_directory_unlocked();
    if (!synchronized) return rollback(synchronized.error());

    stores_.erase(std::string(name));

    // The directory sync above is the deletion commit point. Tombstone removal
    // is post-commit garbage collection: failure must not report that the
    // already committed delete rolled back. Any leftovers are retried during
    // the next configure() by scavenge_delete_tombstones_unlocked().
    bool cleaned = false;
    for (const auto& entry : moved) {
        if (::unlink(entry.tombstone.c_str()) == 0) {
            cleaned = true;
        }
    }
    if (cleaned) {
        (void)sync_directory_unlocked(false);
    }
    mutation_generation_.fetch_add(1, std::memory_order_relaxed);
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
        if (!store) {
            if (store.error().code == ErrorCode::class_not_found) continue;
            return std::unexpected(store.error());
        }
        const std::string current_path = path_for_name(store->name);
        if (path != current_path &&
            path == legacy_path_for_name(store->name)) {
            auto migrated = migrate_legacy_path_unlocked(*store, path);
            if (!migrated) return std::unexpected(migrated.error());
        }
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

    const i32 previous_next_id = (*store)->next_record_id;
    const i32 previous_version = (*store)->version;
    const i64 previous_modified = (*store)->last_modified_ms;
    const u32 previous_format = (*store)->storage_format;
    const i32 id = (*store)->next_record_id++;
    (*store)->records.emplace(id,
                              std::vector<u8>(bytes.begin(), bytes.end()));
    ++(*store)->version;
    (*store)->last_modified_ms = next_modified_time(
        (*store)->last_modified_ms);
    (*store)->storage_format = kCurrentFormatVersion;
    auto persisted = commit_mutation_unlocked(**store);
    if (!persisted) {
        (*store)->records.erase(id);
        (*store)->next_record_id = previous_next_id;
        (*store)->version = previous_version;
        (*store)->last_modified_ms = previous_modified;
        (*store)->storage_format = previous_format;
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

    const i32 previous_version = (*store)->version;
    const i64 previous_modified = (*store)->last_modified_ms;
    const u32 previous_format = (*store)->storage_format;
    std::vector<u8> replacement(bytes.begin(), bytes.end());
    record->second.swap(replacement);
    ++(*store)->version;
    (*store)->last_modified_ms = next_modified_time(
        (*store)->last_modified_ms);
    (*store)->storage_format = kCurrentFormatVersion;
    auto persisted = commit_mutation_unlocked(**store);
    if (!persisted) {
        record->second.swap(replacement);
        (*store)->version = previous_version;
        (*store)->last_modified_ms = previous_modified;
        (*store)->storage_format = previous_format;
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

    const i32 previous_version = (*store)->version;
    const i64 previous_modified = (*store)->last_modified_ms;
    const u32 previous_format = (*store)->storage_format;
    auto removed = (*store)->records.extract(record);
    ++(*store)->version;
    (*store)->last_modified_ms = next_modified_time(
        (*store)->last_modified_ms);
    (*store)->storage_format = kCurrentFormatVersion;
    auto persisted = commit_mutation_unlocked(**store);
    if (!persisted) {
        (*store)->records.insert(std::move(removed));
        (*store)->version = previous_version;
        (*store)->last_modified_ms = previous_modified;
        (*store)->storage_format = previous_format;
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
    const std::string& canonical_path,
    std::optional<std::string_view> expected_name) const {
    struct Candidate final {
        Store store;
        std::string path;
    };

    std::optional<Candidate> canonical;
    std::optional<Candidate> temporary;
    std::optional<Candidate> backup;
    std::optional<Error> first_error;
    bool found_file = false;
    const std::array<std::string, 3> paths {
        canonical_path,
        canonical_path + ".tmp",
        canonical_path + ".bak",
    };
    for (usize index = 0; index < paths.size(); ++index) {
        std::error_code exists_error;
        if (!std::filesystem::exists(paths[index], exists_error)) {
            if (exists_error && !first_error.has_value()) {
                first_error = Error::make(
                    ErrorCode::io_error,
                    "failed to inspect RMS recovery candidate: " +
                        exists_error.message());
            }
            continue;
        }
        found_file = true;
        auto store = read_file_unlocked(paths[index]);
        if (!store) {
            if (!first_error.has_value() || index == 0U) {
                first_error = store.error();
            }
            continue;
        }
        const bool name_matches = expected_name.has_value()
            ? store->name == *expected_name
            : path_for_name(store->name) == canonical_path ||
                legacy_path_for_name(store->name) == canonical_path;
        if (!name_matches) {
            if (!first_error.has_value() || index == 0U) {
                first_error = Error::make(
                    ErrorCode::malformed_archive,
                    "RMS recovery candidate has a mismatched store name");
            }
            continue;
        }
        Candidate candidate {
            .store = std::move(*store),
            .path = paths[index],
        };
        if (index == 0U) {
            canonical = std::move(candidate);
        } else if (index == 1U) {
            temporary = std::move(candidate);
        } else {
            backup = std::move(candidate);
        }
    }

    // A valid canonical file is the committed generation. A backup is the
    // previous committed generation and therefore wins over a leftover tmp.
    // The temporary file is only a last-resort recovery source when neither
    // committed generation remains readable.
    std::optional<Candidate> selected;
    if (canonical.has_value()) {
        selected = std::move(canonical);
    } else if (backup.has_value()) {
        selected = std::move(backup);
    } else if (temporary.has_value()) {
        selected = std::move(temporary);
    }

    if (!selected.has_value()) {
        if (first_error.has_value()) return std::unexpected(*first_error);
        return fail(found_file ? ErrorCode::malformed_archive
                               : ErrorCode::class_not_found,
                    found_file ? "no valid RMS recovery candidate exists"
                               : "RMS store does not exist");
    }

    if (selected->path != canonical_path) {
        if (::rename(selected->path.c_str(), canonical_path.c_str()) != 0) {
            return fail(ErrorCode::io_error,
                        "failed to restore the selected RMS generation");
        }
        auto synchronized = sync_directory_unlocked(false);
        if (!synchronized) return std::unexpected(synchronized.error());
        selected->path = canonical_path;
    }

    if (selected->store.storage_format != kCurrentFormatVersion) {
        auto migrated = persist_unlocked(selected->store);
        if (!migrated) return std::unexpected(migrated.error());
        selected->store.storage_format = kCurrentFormatVersion;
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
            auto synchronized = sync_directory_unlocked(false);
            if (!synchronized) return std::unexpected(synchronized.error());
        }
    }
    return std::move(selected->store);
}

Status RecordStoreRegistry::commit_mutation_unlocked(Store& store) const {
    if (!write_through_) {
        if (!store.pending_persist) {
            store.pending_persist = true;
            store.pending_since = std::chrono::steady_clock::now();
        }
        return {};
    }
    return persist_unlocked(store);
}

void RecordStoreRegistry::set_write_through(bool enabled) noexcept {
    std::scoped_lock lock(mutex_);
    write_through_ = enabled;
}

void RecordStoreRegistry::flush_pending() {
    std::scoped_lock lock(mutex_);
    // ponytail: fixed 250ms pending window flushed on the VM thread; an
    // async flusher thread would shave the remaining hitch but costs a
    // thread on every platform including wasm.
    constexpr auto kPendingFlushDelay = std::chrono::milliseconds(250);
    const auto now = std::chrono::steady_clock::now();
    for (auto& [name, store] : stores_) {
        (void)name;
        if (!store.pending_persist || store.open_count == 0U) continue;
        if (now - store.pending_since < kPendingFlushDelay) continue;
        if (persist_unlocked(store).has_value())
            store.pending_persist = false;
    }
}

Status RecordStoreRegistry::flush_all() {
    std::scoped_lock lock(mutex_);
    for (auto& [name, store] : stores_) {
        (void)name;
        if (!store.pending_persist) continue;
        auto persisted = persist_unlocked(store);
        if (!persisted) return persisted;
        store.pending_persist = false;
    }
    return {};
}

Status RecordStoreRegistry::persist_unlocked(const Store& store) const {
    if (store.version < 0 || store.next_record_id <= 0 ||
        store.last_modified_ms < 0 ||
        store.records.size() > std::numeric_limits<u32>::max()) {
        return fail(ErrorCode::overflow,
                    "RMS metadata is outside the serializable range");
    }

    u64 payload_size = 28U + static_cast<u64>(store.name.size());
    for (const auto& [id, bytes] : store.records) {
        if (id <= 0 || bytes.size() > std::numeric_limits<u32>::max()) {
            return fail(ErrorCode::overflow,
                        "RMS record is too large or has an invalid ID");
        }
        payload_size += 8U + static_cast<u64>(bytes.size());
        if (payload_size > std::numeric_limits<u32>::max()) {
            return fail(ErrorCode::overflow,
                        "RMS store is too large to serialize");
        }
    }

    const auto name_length = encode_u32(
        static_cast<u32>(store.name.size()));
    const auto version = encode_u32(static_cast<u32>(store.version));
    const auto next_id = encode_u32(
        static_cast<u32>(store.next_record_id));
    const auto modified = encode_u64(
        static_cast<u64>(store.last_modified_ms));
    const auto feature_flags = encode_u32(kSupportedFeatureFlags);
    const auto record_count = encode_u32(
        static_cast<u32>(store.records.size()));
    const std::span<const u8> name_bytes(
        reinterpret_cast<const u8*>(store.name.data()),
        store.name.size());

    // Build one immutable payload and checksum those exact bytes. Besides
    // reducing many tiny write() calls, this prevents the durable header and
    // payload from ever being produced from different snapshots if another
    // RMS user becomes active while persistence is being extended later.
    std::vector<u8> payload;
    payload.reserve(static_cast<usize>(payload_size));
    const auto append = [&payload](std::span<const u8> bytes) {
        payload.insert(payload.end(), bytes.begin(), bytes.end());
    };
    append(name_length);
    append(name_bytes);
    append(version);
    append(next_id);
    append(modified);
    append(feature_flags);
    append(record_count);
    for (const auto& [id, bytes] : store.records) {
        const auto encoded_id = encode_u32(static_cast<u32>(id));
        const auto encoded_length = encode_u32(
            static_cast<u32>(bytes.size()));
        append(encoded_id);
        append(encoded_length);
        append(bytes);
    }
    if (payload.size() != static_cast<usize>(payload_size)) {
        return fail(ErrorCode::internal_error,
                    "RMS serialized payload size is inconsistent");
    }

    const uLong checksum = ::crc32(
        ::crc32(0L, Z_NULL, 0), payload.data(),
        static_cast<uInt>(payload.size()));

    std::array<u8, 16> header {};
    std::copy(kMagic.begin(), kMagic.end(), header.begin());
    const auto format = encode_u32(kCurrentFormatVersion);
    const auto encoded_payload_size = encode_u32(
        static_cast<u32>(payload_size));
    const auto encoded_checksum = encode_u32(
        static_cast<u32>(checksum));
    std::copy(format.begin(), format.end(), header.begin() + 4);
    std::copy(encoded_payload_size.begin(), encoded_payload_size.end(),
              header.begin() + 8);
    std::copy(encoded_checksum.begin(), encoded_checksum.end(),
              header.begin() + 12);

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
    const auto abort_temporary = [&](Status status) -> Status {
        (void)::close(descriptor);
        (void)::unlink(temporary.c_str());
        return status;
    };
    const auto write_piece = [&](std::span<const u8> bytes) -> Status {
        auto injected = inject_fault_unlocked(RecordStoreFaultPoint::write);
        if (!injected) return injected;
        return write_all(descriptor, bytes);
    };

    auto written = write_piece(header);
    if (!written) return abort_temporary(std::move(written));
    written = write_piece(payload);
    if (!written) return abort_temporary(std::move(written));

    auto injected = inject_fault_unlocked(RecordStoreFaultPoint::file_sync);
    if (!injected) return abort_temporary(std::move(injected));
    if (::fsync(descriptor) != 0) {
        return abort_temporary(fail(
            ErrorCode::io_error,
            "failed to synchronize RMS file"));
    }
    injected = inject_fault_unlocked(
        RecordStoreFaultPoint::after_file_sync);
    if (!injected) return abort_temporary(std::move(injected));
    if (::close(descriptor) != 0) {
        (void)::unlink(temporary.c_str());
        return fail(ErrorCode::io_error,
                    "failed to close RMS file");
    }

    (void)::unlink(backup.c_str());
    bool has_backup = false;
    injected = inject_fault_unlocked(RecordStoreFaultPoint::backup_link);
    if (!injected) {
        (void)::unlink(temporary.c_str());
        return injected;
    }
#if defined(__EMSCRIPTEN__)
    // Browser persistence is already transactional at the IDBFS layer. Keeping
    // an additional byte-for-byte .bak generation in MEMFS doubles the live
    // RMS payload during every setRecord/addRecord and is especially expensive
    // because the whole store is serialized above. Commit the temp file with a
    // MEMFS rename and let the debounced IDBFS sync provide the durable boundary.
    // Native targets keep the stronger hard-link backup path below.
    (void)backup;
#else
    if (::link(path.c_str(), backup.c_str()) == 0) {
        has_backup = true;
        injected = inject_fault_unlocked(
            RecordStoreFaultPoint::after_backup_link);
        if (!injected) {
            (void)::unlink(temporary.c_str());
            (void)::unlink(backup.c_str());
            return injected;
        }
    } else if (errno != ENOENT) {
        (void)::unlink(temporary.c_str());
        return fail(ErrorCode::io_error,
                    "failed to preserve the previous RMS generation");
    }
#endif

    injected = inject_fault_unlocked(RecordStoreFaultPoint::rename);
    if (!injected) {
        (void)::unlink(temporary.c_str());
        if (has_backup) (void)::unlink(backup.c_str());
        return injected;
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        (void)::unlink(temporary.c_str());
        if (has_backup) (void)::unlink(backup.c_str());
        return fail(ErrorCode::io_error,
                    "failed to atomically replace RMS file");
    }

    const auto rollback_disk = [&](const Error& original) -> Status {
        bool restored = false;
        if (has_backup) {
            restored = ::rename(backup.c_str(), path.c_str()) == 0;
        } else {
            restored = ::unlink(path.c_str()) == 0 || errno == ENOENT;
        }
        auto synchronized = sync_directory_unlocked(false);
        if (!restored || !synchronized) {
            return fail(ErrorCode::io_error,
                        "failed to roll back an RMS persistence failure");
        }
        return std::unexpected(original);
    };

    injected = inject_fault_unlocked(RecordStoreFaultPoint::after_rename);
    if (!injected) return rollback_disk(injected.error());
    auto synchronized = sync_directory_unlocked();
    if (!synchronized) return rollback_disk(synchronized.error());

    if (has_backup) {
        (void)::unlink(backup.c_str());
        (void)sync_directory_unlocked(false);
    }
    mutation_generation_.fetch_add(1, std::memory_order_relaxed);
    return {};
}

Status RecordStoreRegistry::sync_directory_unlocked(bool inject_fault) const {
    const int descriptor = ::open(root_directory_.c_str(),
                                  O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return fail(ErrorCode::io_error,
                    "failed to open RMS directory for synchronization");
    }
    if (inject_fault) {
        auto injected = inject_fault_unlocked(
            RecordStoreFaultPoint::directory_sync);
        if (!injected) {
            (void)::close(descriptor);
            return injected;
        }
    }
    if (::fsync(descriptor) != 0) {
        const int saved_errno = errno;
        (void)::close(descriptor);
        if (saved_errno != EINVAL && saved_errno != ENOTSUP) {
            return fail(ErrorCode::io_error,
                        "failed to synchronize RMS directory");
        }
    } else if (::close(descriptor) != 0) {
        return fail(ErrorCode::io_error,
                    "failed to close RMS directory");
    }
    if (inject_fault) {
        auto injected = inject_fault_unlocked(
            RecordStoreFaultPoint::after_directory_sync);
        if (!injected) return injected;
    }
    return {};
}

Status RecordStoreRegistry::scavenge_delete_tombstones_unlocked() const {
    if (root_directory_.empty()) {
        return fail(ErrorCode::not_configured,
                    "RMS registry is not configured");
    }

    std::error_code error;
    std::filesystem::directory_iterator iterator(root_directory_, error);
    const std::filesystem::directory_iterator end;
    if (error) {
        return fail(ErrorCode::io_error,
                    "failed to inspect RMS delete tombstones: " +
                        error.message());
    }

    bool removed = false;
    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            return fail(ErrorCode::io_error,
                        "failed while inspecting RMS delete tombstones: " +
                            error.message());
        }
        if (!is_delete_tombstone_candidate(iterator->path())) continue;

        std::error_code status_error;
        const auto status = iterator->symlink_status(status_error);
        if (status_error) {
            return fail(ErrorCode::io_error,
                        "failed to inspect RMS delete tombstone: " +
                            status_error.message());
        }
        if (!std::filesystem::is_regular_file(status) &&
            !std::filesystem::is_symlink(status)) {
            continue;
        }

        const std::string path = iterator->path().string();
        if (::unlink(path.c_str()) == 0) {
            removed = true;
        } else if (errno != ENOENT) {
            return fail(ErrorCode::io_error,
                        "failed to remove stale RMS delete tombstone");
        }
    }
    return removed ? sync_directory_unlocked(false) : Status {};
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
        const std::string current_path = path_for_name(store->name);
        if (path != current_path &&
            path == legacy_path_for_name(store->name)) {
            auto migrated = migrate_legacy_path_unlocked(*store, path);
            if (!migrated) return std::unexpected(migrated.error());
        }
        const usize used = used_bytes_unlocked(*store);
        if (used > std::numeric_limits<usize>::max() - total) {
            return fail(ErrorCode::overflow,
                        "RMS suite size overflowed");
        }
        total += used;
        loaded_names.insert(store->name);
    }
    return total;
}

Result<bool> RecordStoreRegistry::remove_path_family_unlocked(
    const std::string& canonical_path) const {
    bool removed = false;
    for (const std::string& suffix : {std::string {},
                                      std::string {".tmp"},
                                      std::string {".bak"}}) {
        if (::unlink((canonical_path + suffix).c_str()) == 0) {
            removed = true;
        } else if (errno != ENOENT) {
            return fail(ErrorCode::io_error,
                        "failed to remove RMS storage file");
        }
    }
    return removed;
}

Status RecordStoreRegistry::migrate_legacy_path_unlocked(
    const Store& store,
    const std::string& legacy_path) const {
    if (legacy_path == path_for_name(store.name)) return {};
    auto persisted = persist_unlocked(store);
    if (!persisted) return persisted;
    auto removed = remove_path_family_unlocked(legacy_path);
    if (!removed) return std::unexpected(removed.error());
    if (*removed) return sync_directory_unlocked();
    return {};
}

Status RecordStoreRegistry::inject_fault_unlocked(
    RecordStoreFaultPoint point) const {
    if (!fault_injector_) return {};
    return fault_injector_(point);
}

std::string RecordStoreRegistry::path_for_name(
    std::string_view name) const {
    return root_directory_ + "/rms-" + base64url_encode(name) + ".rms";
}

std::string RecordStoreRegistry::legacy_path_for_name(
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
