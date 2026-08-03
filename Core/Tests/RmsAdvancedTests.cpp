#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <zlib.h>

#include "phoneme/network/AsyncNetworkAdapter.hpp"
#include "phoneme/runtime/RecordStoreRegistry.hpp"
#include "phoneme/vm/ClassRepository.hpp"
#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::network {

std::shared_ptr<AsyncNetworkAdapter> make_posix_network_adapter() {
    return {};
}

} // namespace phoneme::network

namespace {

namespace fs = std::filesystem;
using phoneme::ErrorCode;
using phoneme::i32;
using phoneme::u8;
using phoneme::u32;
using phoneme::u64;
using phoneme::usize;
using phoneme::runtime::RecordStoreFaultPoint;
using phoneme::runtime::RecordStoreRegistry;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

void clear_directory(const fs::path& path) {
    std::error_code error;
    fs::remove_all(path, error);
    require(!error, "remove RMS test directory");
    fs::create_directories(path, error);
    require(!error, "create RMS test directory");
}

[[nodiscard]] std::vector<u8> read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(input.good(), "open RMS file for reading");
    const auto end = input.tellg();
    require(end >= 0, "inspect RMS file size");
    std::vector<u8> bytes(static_cast<usize>(end));
    input.seekg(0, std::ios::beg);
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    require(input.good() || input.eof(), "read RMS file");
    return bytes;
}

void write_bytes(const fs::path& path, std::span<const u8> bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "open RMS file for writing");
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    output.flush();
    require(output.good(), "write RMS file");
}

[[nodiscard]] fs::path only_canonical_file(const fs::path& root) {
    std::vector<fs::path> candidates;
    for (const auto& entry : fs::directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.ends_with(".rms") &&
            !name.ends_with(".rms.tmp") &&
            !name.ends_with(".rms.bak")) {
            candidates.push_back(entry.path());
        }
    }
    require(candidates.size() == 1U, "find one canonical RMS file");
    return candidates.front();
}

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

[[nodiscard]] u64 fnv1a(std::string_view value) noexcept {
    u64 hash = 1469598103934665603ULL;
    for (const char character : value) {
        hash ^= static_cast<u8>(static_cast<unsigned char>(character));
        hash *= 1099511628211ULL;
    }
    return hash;
}

[[nodiscard]] std::string hexadecimal_u64(u64 value) {
    constexpr std::array<char, 16> digits {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    std::string result(16U, '0');
    for (usize index = 0; index < result.size(); ++index) {
        const usize shift = (result.size() - 1U - index) * 4U;
        result[index] = digits[static_cast<usize>((value >> shift) & 0xFU)];
    }
    return result;
}

[[nodiscard]] fs::path legacy_path(const fs::path& root,
                                   std::string_view name) {
    return root / (hexadecimal_u64(fnv1a(name)) + ".rms");
}

[[nodiscard]] std::vector<u8> legacy_v1_file(
    std::string_view name,
    i32 version,
    i32 next_id,
    u64 modified,
    const std::vector<std::pair<i32, std::vector<u8>>>& records) {
    std::vector<u8> payload;
    append_u32(payload, static_cast<u32>(name.size()));
    payload.insert(payload.end(), name.begin(), name.end());
    append_u32(payload, static_cast<u32>(version));
    append_u32(payload, static_cast<u32>(next_id));
    append_u64(payload, modified);
    append_u32(payload, static_cast<u32>(records.size()));
    for (const auto& [id, bytes] : records) {
        append_u32(payload, static_cast<u32>(id));
        append_u32(payload, static_cast<u32>(bytes.size()));
        payload.insert(payload.end(), bytes.begin(), bytes.end());
    }
    const uLong checksum = ::crc32(
        ::crc32(0L, Z_NULL, 0), payload.data(),
        static_cast<uInt>(payload.size()));
    std::vector<u8> file {'P', 'M', 'R', 'S'};
    append_u32(file, 1U);
    append_u32(file, static_cast<u32>(payload.size()));
    append_u32(file, static_cast<u32>(checksum));
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

[[nodiscard]] std::vector<u8> corrupted(std::vector<u8> bytes) {
    require(!bytes.empty(), "corrupt non-empty RMS generation");
    bytes.back() ^= 0x5AU;
    return bytes;
}

void test_suite_quota(const fs::path& root) {
    clear_directory(root);
    RecordStoreRegistry registry(100U);
    require(registry.configure(root.string()).has_value(),
            "configure quota registry");
    require(registry.open("alpha", true).has_value(), "open quota alpha");
    require(registry.open("beta", true).has_value(), "open quota beta");

    const std::vector<u8> forty(40U, 1U);
    const std::vector<u8> fifty(50U, 2U);
    const std::vector<u8> ten(10U, 3U);
    const std::vector<u8> eleven(11U, 4U);
    require(registry.add_record("alpha", forty).has_value(),
            "consume first suite quota segment");
    require(registry.add_record("beta", fifty).has_value(),
            "consume second suite quota segment");
    auto available = registry.available_bytes("alpha");
    require(available.has_value() && *available == 10U,
            "suite quota is shared across stores");
    auto rejected = registry.add_record("alpha", eleven);
    require(!rejected.has_value() &&
                rejected.error().code == ErrorCode::overflow,
            "quota rejects one byte beyond boundary");
    require(registry.add_record("beta", ten).has_value(),
            "quota accepts exact remaining bytes");
    available = registry.available_bytes("beta");
    require(available.has_value() && *available == 0U,
            "suite quota reaches exact zero boundary");
    require(registry.close("alpha").has_value(), "close quota alpha");
    require(registry.close("beta").has_value(), "close quota beta");
}

void test_concurrent_registry(const fs::path& root) {
    clear_directory(root);
    RecordStoreRegistry registry(8U * 1024U * 1024U);
    require(registry.configure(root.string()).has_value(),
            "configure concurrent registry");
    require(registry.open("concurrent", true).has_value(),
            "create concurrent store");
    require(registry.close("concurrent").has_value(),
            "close initial concurrent handle");

    constexpr usize thread_count = 4U;
    constexpr usize records_per_thread = 30U;
    std::atomic<bool> success {true};
    std::mutex ids_mutex;
    std::vector<i32> ids;
    ids.reserve(thread_count * records_per_thread);
    std::vector<std::thread> threads;
    for (usize thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([&, thread_index] {
            if (!registry.open("concurrent", false)) {
                success.store(false);
                return;
            }
            for (usize index = 0; index < records_per_thread; ++index) {
                const std::array<u8, 16> payload {
                    static_cast<u8>(thread_index),
                    static_cast<u8>(index),
                };
                auto id = registry.add_record("concurrent", payload);
                if (!id) {
                    success.store(false);
                    break;
                }
                std::scoped_lock lock(ids_mutex);
                ids.push_back(*id);
            }
            if (!registry.close("concurrent")) success.store(false);
        });
    }
    for (auto& thread : threads) thread.join();
    require(success.load(), "parallel RMS add operations complete");
    require(ids.size() == thread_count * records_per_thread,
            "parallel RMS add preserves all records");
    std::sort(ids.begin(), ids.end());
    require(std::adjacent_find(ids.begin(), ids.end()) == ids.end(),
            "parallel RMS add allocates unique record IDs");

    threads.clear();
    for (usize thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads.emplace_back([&, thread_index] {
            if (!registry.open("concurrent", false)) {
                success.store(false);
                return;
            }
            for (usize index = thread_index; index < ids.size();
                 index += thread_count) {
                if ((ids[index] & 1) == 0) {
                    const std::array<u8, 32> replacement {7U, 8U, 9U};
                    if (!registry.set_record(
                            "concurrent", ids[index], replacement)) {
                        success.store(false);
                    }
                } else if (!registry.delete_record("concurrent", ids[index])) {
                    success.store(false);
                }
            }
            if (!registry.close("concurrent")) success.store(false);
        });
    }
    for (auto& thread : threads) thread.join();
    require(success.load(), "parallel RMS set/delete operations complete");

    require(registry.open("concurrent", false).has_value(),
            "reopen concurrent store for verification");
    auto info = registry.info("concurrent");
    require(info.has_value() &&
                info->record_count == ids.size() / 2U &&
                info->next_record_id ==
                    static_cast<i32>(ids.size() + 1U),
            "parallel RMS mutation preserves count and monotonic IDs");
    require(registry.close("concurrent").has_value(),
            "close verified concurrent store");
}

void test_fault_rollback(const fs::path& root) {
    const std::array<RecordStoreFaultPoint, 7> points {
        RecordStoreFaultPoint::write,
        RecordStoreFaultPoint::file_sync,
        RecordStoreFaultPoint::backup_link,
        RecordStoreFaultPoint::rename,
        RecordStoreFaultPoint::directory_sync,
        RecordStoreFaultPoint::after_rename,
        RecordStoreFaultPoint::after_directory_sync,
    };
    for (usize index = 0; index < points.size(); ++index) {
        const fs::path case_root = root / std::to_string(index);
        clear_directory(case_root);
        RecordStoreRegistry registry;
        require(registry.configure(case_root.string()).has_value(),
                "configure fault rollback registry");
        require(registry.open("rollback", true).has_value(),
                "create rollback store");
        constexpr std::array<u8, 2> baseline {1U, 2U};
        auto baseline_id = registry.add_record("rollback", baseline);
        require(baseline_id.has_value() && *baseline_id == 1,
                "write rollback baseline");
        auto before = registry.info("rollback");
        require(before.has_value(), "inspect rollback baseline");

        bool fired = false;
        registry.set_fault_injector(
            [&, target = points[index]](RecordStoreFaultPoint point)
                -> phoneme::Status {
                if (!fired && point == target) {
                    fired = true;
                    return phoneme::fail(ErrorCode::io_error,
                                         "injected RMS persistence failure");
                }
                return {};
            });
        constexpr std::array<u8, 3> candidate {3U, 4U, 5U};
        auto added = registry.add_record("rollback", candidate);
        require(!added.has_value() && fired,
                "fault injection rejects RMS mutation");
        registry.clear_fault_injector();
        auto after = registry.info("rollback");
        require(after.has_value() &&
                    after->version == before->version &&
                    after->record_count == before->record_count &&
                    after->next_record_id == before->next_record_id,
                "failed persist rolls back in-memory metadata");
        auto baseline_record = registry.record("rollback", 1);
        require(baseline_record.has_value() &&
                    *baseline_record ==
                        std::vector<u8>(baseline.begin(), baseline.end()),
                "failed persist preserves in-memory record data");
        require(registry.close("rollback").has_value(),
                "close rollback registry");

        RecordStoreRegistry reloaded;
        require(reloaded.configure(case_root.string()).has_value(),
                "configure rollback reload");
        require(reloaded.open("rollback", false).has_value(),
                "reopen rollback store from disk");
        auto disk = reloaded.info("rollback");
        require(disk.has_value() &&
                    disk->version == before->version &&
                    disk->record_count == before->record_count &&
                    disk->next_record_id == before->next_record_id,
                "failed persist rolls back durable generation");
        require(reloaded.close("rollback").has_value(),
                "close rollback reload");
    }
}

void test_delete_store_rollback(const fs::path& root) {
    const std::array<RecordStoreFaultPoint, 2> points {
        RecordStoreFaultPoint::directory_sync,
        RecordStoreFaultPoint::after_directory_sync,
    };
    for (usize index = 0; index < points.size(); ++index) {
        const fs::path case_root = root / std::to_string(index);
        clear_directory(case_root);
        RecordStoreRegistry registry;
        require(registry.configure(case_root.string()).has_value(),
                "configure delete rollback registry");
        require(registry.open("delete-rollback", true).has_value(),
                "create delete rollback store");
        constexpr std::array<u8, 3> baseline {4U, 5U, 6U};
        auto record_id = registry.add_record("delete-rollback", baseline);
        require(record_id.has_value() && *record_id == 1,
                "write delete rollback baseline");
        require(registry.close("delete-rollback").has_value(),
                "close delete rollback baseline");

        bool fired = false;
        registry.set_fault_injector(
            [&, target = points[index]](RecordStoreFaultPoint point)
                -> phoneme::Status {
                if (!fired && point == target) {
                    fired = true;
                    return phoneme::fail(
                        ErrorCode::io_error,
                        "injected RMS delete directory sync failure");
                }
                return {};
            });
        auto deleted = registry.delete_store("delete-rollback");
        require(!deleted.has_value() && fired,
                "delete failure is reported before committing removal");
        registry.clear_fault_injector();

        auto names = registry.list_store_names();
        require(names.has_value() &&
                    std::find(names->begin(), names->end(),
                              "delete-rollback") != names->end(),
                "failed delete remains visible in current registry");
        require(registry.open("delete-rollback", false).has_value(),
                "failed delete remains openable in current registry");
        auto record = registry.record("delete-rollback", 1);
        require(record.has_value() &&
                    *record == std::vector<u8>(baseline.begin(),
                                               baseline.end()),
                "failed delete preserves current registry record data");
        require(registry.close("delete-rollback").has_value(),
                "close current registry after failed delete");

        RecordStoreRegistry reloaded;
        require(reloaded.configure(case_root.string()).has_value(),
                "configure delete rollback reload");
        require(reloaded.open("delete-rollback", false).has_value(),
                "failed delete remains durable after restart");
        auto durable_record = reloaded.record("delete-rollback", 1);
        require(durable_record.has_value() &&
                    *durable_record == std::vector<u8>(baseline.begin(),
                                                       baseline.end()),
                "failed delete restores the durable generation");
        require(reloaded.close("delete-rollback").has_value(),
                "close delete rollback reload");
        require(reloaded.delete_store("delete-rollback").has_value(),
                "delete succeeds after fault injector is removed");
        auto removed_names = reloaded.list_store_names();
        require(removed_names.has_value() && removed_names->empty(),
                "successful delete removes store from listing");
    }
}

void test_delete_tombstone_scavenger(const fs::path& root) {
    clear_directory(root);
    const fs::path stale = root / "orphan.rms.delete-4242";
    const fs::path unrelated = root / "notes.delete-4242";
    write_bytes(stale, std::array<u8, 3> {1U, 2U, 3U});
    write_bytes(unrelated, std::array<u8, 1> {9U});

    RecordStoreRegistry registry;
    require(registry.configure(root.string()).has_value(),
            "configure registry with stale delete tombstone");
    require(!fs::exists(stale),
            "configure scavenges stale RMS delete tombstone");
    require(fs::exists(unrelated),
            "configure preserves unrelated files containing delete marker");
}

struct Generations final {
    std::string filename;
    std::vector<u8> first;
    std::vector<u8> second;
};

[[nodiscard]] Generations make_generations(const fs::path& root) {
    clear_directory(root);
    RecordStoreRegistry registry;
    require(registry.configure(root.string()).has_value(),
            "configure generation source");
    require(registry.open("recover", true).has_value(),
            "create generation source");
    constexpr std::array<u8, 1> first_record {1U};
    require(registry.add_record("recover", first_record).has_value(),
            "write first generation");
    require(registry.close("recover").has_value(),
            "close first generation");
    const fs::path canonical = only_canonical_file(root);
    auto first = read_bytes(canonical);

    require(registry.open("recover", false).has_value(),
            "reopen generation source");
    constexpr std::array<u8, 1> second_record {2U};
    require(registry.add_record("recover", second_record).has_value(),
            "write second generation");
    require(registry.close("recover").has_value(),
            "close second generation");
    auto second = read_bytes(canonical);
    return Generations {
        .filename = canonical.filename().string(),
        .first = std::move(first),
        .second = std::move(second),
    };
}

void install_generation_case(const fs::path& root,
                             const Generations& generations,
                             std::span<const u8> canonical,
                             std::span<const u8> temporary,
                             std::span<const u8> backup) {
    clear_directory(root);
    const fs::path path = root / generations.filename;
    if (!canonical.empty()) write_bytes(path, canonical);
    if (!temporary.empty()) write_bytes(path.string() + ".tmp", temporary);
    if (!backup.empty()) write_bytes(path.string() + ".bak", backup);
}

void verify_recovered_generation(const fs::path& root,
                                 usize expected_records,
                                 i32 expected_version,
                                 i32 expected_next_id) {
    RecordStoreRegistry registry;
    require(registry.configure(root.string()).has_value(),
            "configure recovery registry");
    require(registry.open("recover", false).has_value(),
            "open recovered store");
    auto info = registry.info("recover");
    require(info.has_value() &&
                info->record_count == expected_records &&
                info->version == expected_version &&
                info->next_record_id == expected_next_id,
            "recovery selects expected committed generation");
    require(registry.close("recover").has_value(),
            "close recovered store");
}

void test_recovery_selection(const fs::path& root) {
    const Generations generations = make_generations(root / "source");
    const auto corrupt_first = corrupted(generations.first);
    const auto corrupt_second = corrupted(generations.second);

    install_generation_case(root / "canonical-wins", generations,
                            generations.first, generations.second,
                            generations.first);
    verify_recovered_generation(root / "canonical-wins", 1U, 1, 2);

    install_generation_case(root / "backup-wins", generations,
                            corrupt_second, generations.second,
                            generations.first);
    verify_recovered_generation(root / "backup-wins", 1U, 1, 2);

    install_generation_case(root / "tmp-last-resort", generations,
                            corrupt_first, generations.second,
                            corrupt_first);
    verify_recovered_generation(root / "tmp-last-resort", 2U, 2, 3);

    install_generation_case(root / "new-canonical", generations,
                            generations.second, corrupt_first,
                            generations.first);
    verify_recovered_generation(root / "new-canonical", 2U, 2, 3);

    install_generation_case(root / "all-corrupt", generations,
                            corrupt_first, corrupt_second, corrupt_first);
    RecordStoreRegistry broken;
    require(broken.configure((root / "all-corrupt").string()).has_value(),
            "configure corrupt recovery registry");
    auto opened = broken.open("recover", false);
    require(!opened.has_value() &&
                (opened.error().code == ErrorCode::checksum_mismatch ||
                 opened.error().code == ErrorCode::malformed_archive),
            "recovery rejects all corrupt generations");
    auto listed = broken.list_store_names();
    require(!listed.has_value() &&
                (listed.error().code == ErrorCode::checksum_mismatch ||
                 listed.error().code == ErrorCode::malformed_archive),
            "listing reports an all-corrupt RMS store");
}

void initialize_crash_store(const fs::path& root) {
    clear_directory(root);
    RecordStoreRegistry registry;
    require(registry.configure(root.string()).has_value(),
            "configure crash store");
    require(registry.open("crash-store", true).has_value(),
            "create crash store");
    constexpr std::array<u8, 1> baseline {1U};
    require(registry.add_record("crash-store", baseline).has_value(),
            "write crash baseline");
    require(registry.close("crash-store").has_value(),
            "close crash baseline");
}

void run_crash_child(const fs::path& harness,
                     const fs::path& root,
                     std::string_view checkpoint) {
    const pid_t child = ::fork();
    require(child >= 0, "fork RMS crash harness");
    if (child == 0) {
        const std::string harness_text = harness.string();
        const std::string root_text = root.string();
        const std::string checkpoint_text(checkpoint);
        ::execl(harness_text.c_str(), harness_text.c_str(),
                root_text.c_str(), checkpoint_text.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    int status = 0;
    require(::waitpid(child, &status, 0) == child,
            "wait for RMS crash harness");
    require(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
            "crash harness is killed at requested checkpoint");
}

void test_process_crash_recovery(const fs::path& root,
                                 const fs::path& harness) {
    struct Case final {
        const char* checkpoint;
        usize records;
        i32 version;
        i32 next_id;
    };
    constexpr std::array<Case, 4> cases {{
        {"after-file-sync", 1U, 1, 2},
        {"after-backup-link", 1U, 1, 2},
        {"after-rename", 2U, 2, 3},
        {"after-directory-sync", 2U, 2, 3},
    }};
    for (const Case& entry : cases) {
        const fs::path case_root = root / entry.checkpoint;
        initialize_crash_store(case_root);
        run_crash_child(harness, case_root, entry.checkpoint);
        RecordStoreRegistry recovered;
        require(recovered.configure(case_root.string()).has_value(),
                "configure post-crash registry");
        require(recovered.open("crash-store", false).has_value(),
                "reopen store after killed writer");
        auto info = recovered.info("crash-store");
        require(info.has_value() &&
                    info->record_count == entry.records &&
                    info->version == entry.version &&
                    info->next_record_id == entry.next_id,
                "post-crash recovery chooses durable generation");
        require(recovered.close("crash-store").has_value(),
                "close post-crash store");
    }
}

void test_migration_and_future_version(const fs::path& root) {
    const fs::path legacy_root = root / "legacy";
    clear_directory(legacy_root);
    RecordStoreRegistry registry;
    require(registry.configure(legacy_root.string()).has_value(),
            "configure legacy migration registry");
    const std::string legacy_name = "legacy-store";
    const auto legacy = legacy_v1_file(
        legacy_name, 4, 6, 1234U,
        {{2, {2U, 3U}}, {5, {5U}}});
    const fs::path old_path = legacy_path(legacy_root, legacy_name);
    write_bytes(old_path, legacy);
    require(registry.open(legacy_name, false).has_value(),
            "open legacy v1 store");
    auto info = registry.info(legacy_name);
    require(info.has_value() && info->version == 4 &&
                info->next_record_id == 6 && info->record_count == 2U,
            "legacy v1 metadata survives migration");
    require(registry.close(legacy_name).has_value(),
            "close migrated legacy store");
    require(!fs::exists(old_path), "legacy hashed path is removed");
    const auto migrated = read_bytes(only_canonical_file(legacy_root));
    require(migrated.size() >= 8U && migrated[7] == 2U,
            "legacy store is rewritten in v2 format");

    const fs::path future_root = root / "future";
    clear_directory(future_root);
    RecordStoreRegistry future_writer;
    require(future_writer.configure(future_root.string()).has_value(),
            "configure future-version writer");
    require(future_writer.open("future-store", true).has_value(),
            "create future-version store");
    require(future_writer.close("future-store").has_value(),
            "close future-version store");
    const fs::path future_path = only_canonical_file(future_root);
    auto future_bytes = read_bytes(future_path);
    require(future_bytes.size() >= 8U, "future-version file has header");
    future_bytes[4] = 0U;
    future_bytes[5] = 0U;
    future_bytes[6] = 0U;
    future_bytes[7] = 3U;
    write_bytes(future_path, future_bytes);
    RecordStoreRegistry future_reader;
    require(future_reader.configure(future_root.string()).has_value(),
            "configure future-version reader");
    auto future_opened = future_reader.open("future-store", false);
    require(!future_opened.has_value() &&
                future_opened.error().code == ErrorCode::unsupported_archive,
            "future RMS format is rejected");
    auto future_listed = future_reader.list_store_names();
    require(!future_listed.has_value() &&
                future_listed.error().code == ErrorCode::unsupported_archive,
            "listing reports an unsupported future RMS format");
}

void test_suite_isolation_and_names(const fs::path& root) {
    const fs::path first_root = root / "suite-a";
    const fs::path second_root = root / "suite-b";
    clear_directory(first_root);
    clear_directory(second_root);
    RecordStoreRegistry first;
    RecordStoreRegistry second;
    require(first.configure(first_root.string()).has_value(),
            "configure first isolated suite");
    require(second.configure(second_root.string()).has_value(),
            "configure second isolated suite");
    require(first.open("shared", true).has_value(),
            "open first isolated store");
    require(second.open("shared", true).has_value(),
            "open second isolated store");
    constexpr std::array<u8, 1> first_data {1U};
    constexpr std::array<u8, 2> second_data {2U, 2U};
    require(first.add_record("shared", first_data).has_value(),
            "write first isolated suite");
    require(second.add_record("shared", second_data).has_value(),
            "write second isolated suite");
    auto first_info = first.info("shared");
    auto second_info = second.info("shared");
    require(first_info.has_value() && second_info.has_value() &&
                first_info->total_record_bytes == 1U &&
                second_info->total_record_bytes == 2U,
            "suite roots isolate identical store names");
    require(first.close("shared").has_value(),
            "close first isolated suite");
    require(second.close("shared").has_value(),
            "close second isolated suite");

    const std::string unusual = "../folder/name-\xE2\x98\x83";
    require(first.open(unusual, true).has_value(),
            "open safely encoded unusual RMS name");
    require(first.close(unusual).has_value(),
            "close safely encoded unusual RMS name");
    for (const auto& entry : fs::directory_iterator(first_root)) {
        require(entry.is_regular_file(),
                "encoded RMS name cannot create nested paths");
    }
    auto names = first.list_store_names();
    require(names.has_value() &&
                std::find(names->begin(), names->end(), unusual) != names->end(),
            "encoded RMS name round-trips through listing");

    const std::string source_name = "collision-source";
    const std::string target_name = "collision-target";
    require(first.open(source_name, true).has_value(),
            "create collision source store");
    require(first.close(source_name).has_value(),
            "close collision source store");
    fs::path source_path;
    for (const auto& entry : fs::directory_iterator(first_root)) {
        if (!entry.is_regular_file()) continue;
        const auto bytes = read_bytes(entry.path());
        if (bytes.size() > 16U &&
            std::search(bytes.begin(), bytes.end(),
                        source_name.begin(), source_name.end()) != bytes.end()) {
            source_path = entry.path();
            break;
        }
    }
    require(!source_path.empty(), "find collision source file");
    write_bytes(legacy_path(first_root, target_name), read_bytes(source_path));
    auto collision = first.open(target_name, false);
    require(!collision.has_value() &&
                collision.error().code == ErrorCode::malformed_archive,
            "mismatched legacy hash candidate is rejected");
}

void test_large_store(const fs::path& root) {
    clear_directory(root);
    RecordStoreRegistry registry(8U * 1024U * 1024U);
    require(registry.configure(root.string()).has_value(),
            "configure large RMS store");
    require(registry.open("large", true).has_value(),
            "create large RMS store");
    const std::vector<u8> payload(64U * 1024U, 0xA5U);
    for (usize index = 0; index < 32U; ++index) {
        auto id = registry.add_record("large", payload);
        require(id.has_value() && *id == static_cast<i32>(index + 1U),
                "append large RMS record");
    }
    auto info = registry.info("large");
    require(info.has_value() && info->record_count == 32U &&
                info->total_record_bytes == 2U * 1024U * 1024U,
            "large RMS store reports bounded payload size");
    require(registry.close("large").has_value(),
            "close large RMS store");

    RecordStoreRegistry reloaded(8U * 1024U * 1024U);
    require(reloaded.configure(root.string()).has_value(),
            "configure large RMS reload");
    require(reloaded.open("large", false).has_value(),
            "reload large RMS store");
    auto snapshot = reloaded.snapshot("large");
    require(snapshot.has_value() && snapshot->size() == 32U &&
                snapshot->front().bytes.size() == payload.size() &&
                snapshot->back().bytes.size() == payload.size(),
            "large RMS store reloads without truncation");
    require(reloaded.close("large").has_value(),
            "close large RMS reload");
}

void invoke_fixture_int(phoneme::vm::Machine& machine,
                        const char* method,
                        i32 expected) {
    auto result = machine.invoke_static("corefixture/RmsOps",
                                        method,
                                        "()I",
                                        {},
                                        120'000'000U);
    if (!result) {
        std::cerr << "VM invoke error in " << method << ": "
                  << result.error().message << '\n';
    }
    require(result.has_value(), "invoke RMS fixture through VM");
    if (!result->completed_normally() && result->throwable.has_value()) {
        auto throwable = machine.heap().class_name(*result->throwable);
        if (throwable) {
            std::cerr << method << " threw " << *throwable << '\n';
        }
    }
    require(result->completed_normally() && result->return_value.has_value(),
            "RMS fixture completes normally");
    auto value = result->return_value->as_int();
    require(value.has_value() && *value == expected,
            "RMS fixture returns complete semantic bitmask");
}

void test_vm_semantics(const fs::path& root,
                       const fs::path& fixture_jar) {
    clear_directory(root);
    phoneme::vm::ClassRepository classes;
    require(classes.add_archive(fixture_jar.string()).has_value(),
            "add RMS fixture archive");
    phoneme::vm::Machine machine(classes);
    require(machine.configure_record_store_root(root.string()).has_value(),
            "configure VM RMS root");
    invoke_fixture_int(machine, "advancedSemantics", 1023);
    invoke_fixture_int(machine, "callbackAndCursorSemantics", 511);
    invoke_fixture_int(machine, "concurrentThreadSemantics", 15);
}

} // namespace

int main(int argc, char** argv) {
    require(argc == 4,
            "usage: RmsAdvancedTests <fixture.jar> <crash-harness> <root>");
    const fs::path fixture_jar(argv[1]);
    const fs::path crash_harness(argv[2]);
    const fs::path root(argv[3]);
    clear_directory(root);

    test_suite_quota(root / "quota");
    test_concurrent_registry(root / "concurrent");
    test_fault_rollback(root / "faults");
    test_delete_store_rollback(root / "delete-faults");
    test_delete_tombstone_scavenger(root / "delete-scavenger");
    test_recovery_selection(root / "recovery");
    test_process_crash_recovery(root / "crash", crash_harness);
    test_migration_and_future_version(root / "formats");
    test_suite_isolation_and_names(root / "isolation");
    test_large_store(root / "large");
    test_vm_semantics(root / "vm", fixture_jar);

    std::cout << "RMS advanced tests passed\n";
    return 0;
}
