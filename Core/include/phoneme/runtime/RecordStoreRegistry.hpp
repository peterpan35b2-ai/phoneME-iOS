#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/base/Error.hpp"

namespace phoneme::runtime {

struct RecordStoreInfo final {
    std::string name;
    i32 version {0};
    i32 next_record_id {1};
    i64 last_modified_ms {0};
    usize total_record_bytes {0};
    usize record_count {0};
};

struct RecordSnapshot final {
    i32 id {0};
    std::vector<u8> bytes;
};

enum class RecordStoreFaultPoint : u8 {
    write,
    file_sync,
    backup_link,
    rename,
    directory_sync,
    after_file_sync,
    after_backup_link,
    after_rename,
    after_directory_sync,
};

using RecordStoreFaultInjector =
    std::function<Status(RecordStoreFaultPoint)>;

class RecordStoreRegistry final {
public:
    explicit RecordStoreRegistry(usize quota_bytes = 32U * 1024U * 1024U)
        : quota_bytes_(quota_bytes) {}

    [[nodiscard]] Status configure(std::string root_directory);
    [[nodiscard]] Status open(std::string_view name, bool create);
    [[nodiscard]] Status close(std::string_view name);
    [[nodiscard]] Status delete_store(std::string_view name);
    [[nodiscard]] Result<std::vector<std::string>> list_store_names();

    [[nodiscard]] Result<RecordStoreInfo> info(std::string_view name);
    [[nodiscard]] Result<i32> add_record(std::string_view name,
                                         std::span<const u8> bytes);
    [[nodiscard]] Status set_record(std::string_view name,
                                    i32 record_id,
                                    std::span<const u8> bytes);
    [[nodiscard]] Status delete_record(std::string_view name,
                                       i32 record_id);
    [[nodiscard]] Result<std::vector<u8>> record(std::string_view name,
                                                 i32 record_id);
    [[nodiscard]] Result<std::vector<RecordSnapshot>> snapshot(
        std::string_view name);
    [[nodiscard]] Result<usize> available_bytes(std::string_view name);
    [[nodiscard]] u64 mutation_generation() const noexcept {
        return mutation_generation_.load(std::memory_order_relaxed);
    }

    void set_fault_injector(RecordStoreFaultInjector injector);
    void clear_fault_injector();

    // Write-through mode persists synchronously on every mutation (default,
    // keeps the fault-rollback contract used by tests). Deferred mode marks
    // stores dirty instead; flush_pending() persists dirty stores once the
    // pending window elapses, close()/flush_all() force a flush. The VM uses
    // deferred mode so per-record fsync pairs cannot stall the game thread.
    void set_write_through(bool enabled) noexcept;
    void flush_pending();
    [[nodiscard]] Status flush_all();

private:
    struct Store final {
        std::string name;
        u32 storage_format {0};
        i32 version {0};
        i32 next_record_id {1};
        i64 last_modified_ms {0};
        usize open_count {0};
        std::map<i32, std::vector<u8>> records;
        bool pending_persist {false};
        std::chrono::steady_clock::time_point pending_since {};
    };

    [[nodiscard]] static Status validate_name(std::string_view name);
    [[nodiscard]] Result<Store*> require_open_unlocked(std::string_view name);
    [[nodiscard]] Result<Store*> load_unlocked(std::string_view name,
                                               bool create);
    [[nodiscard]] Result<Store> read_file_unlocked(
        const std::string& path) const;
    [[nodiscard]] Result<Store> recover_file_unlocked(
        const std::string& canonical_path,
        std::optional<std::string_view> expected_name = std::nullopt) const;
    [[nodiscard]] Status persist_unlocked(const Store& store) const;
    [[nodiscard]] Status commit_mutation_unlocked(Store& store) const;
    [[nodiscard]] Status sync_directory_unlocked(
        bool inject_fault = true) const;
    [[nodiscard]] Status scavenge_delete_tombstones_unlocked() const;
    [[nodiscard]] Result<usize> suite_used_bytes_unlocked() const;
    [[nodiscard]] Result<bool> remove_path_family_unlocked(
        const std::string& canonical_path) const;
    [[nodiscard]] Status migrate_legacy_path_unlocked(
        const Store& store,
        const std::string& legacy_path) const;
    [[nodiscard]] Status inject_fault_unlocked(
        RecordStoreFaultPoint point) const;
    [[nodiscard]] std::string path_for_name(std::string_view name) const;
    [[nodiscard]] std::string legacy_path_for_name(
        std::string_view name) const;
    [[nodiscard]] usize used_bytes_unlocked(const Store& store) const noexcept;
    [[nodiscard]] static i64 current_time_millis() noexcept;

    usize quota_bytes_ {0};
    bool write_through_ {true};
    std::string root_directory_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Store> stores_;
    RecordStoreFaultInjector fault_injector_;
    mutable std::atomic<u64> mutation_generation_ {0};
};

} // namespace phoneme::runtime
