#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>
#include <thread>

#include "phoneme/base/Types.hpp"
#include "phoneme/runtime/WorkCoordinator.hpp"

namespace phoneme::runtime {

// Small persistent host compute pool for deterministic native work that is
// independent of Java heap mutation. The caller participates in every job, so
// worker threads add CPU capacity instead of replacing the foreground thread.
class ParallelExecutor final {
public:
    using Task = std::function<void(usize begin, usize end)>;

    explicit ParallelExecutor(u32 worker_limit = 0U) noexcept;
    ~ParallelExecutor();

    ParallelExecutor(const ParallelExecutor&) = delete;
    ParallelExecutor& operator=(const ParallelExecutor&) = delete;

    void parallel_for(usize item_count,
                      usize minimum_parallel_items,
                      usize chunk_items,
                      const Task& task);
    void parallel_for(WorkClass work_class,
                      usize item_count,
                      usize minimum_parallel_items,
                      usize chunk_items,
                      const Task& task);
    void parallel_for(WorkClass work_class,
                      usize work_units,
                      usize item_count,
                      usize minimum_parallel_items,
                      usize chunk_items,
                      const Task& task);

    [[nodiscard]] u32 worker_limit() const noexcept { return worker_limit_; }
    [[nodiscard]] u32 active_worker_count() const noexcept;

private:
    void ensure_workers();
    void worker_loop(u32 worker_index) noexcept;
    void drain_current_job() noexcept;

    const u32 worker_limit_;
    std::mutex submission_mutex_;
    mutable std::mutex state_mutex_;
    std::condition_variable work_condition_;
    std::condition_variable completion_condition_;
    std::vector<std::thread> workers_;
    const Task* current_task_ {nullptr};
    usize current_item_count_ {0U};
    usize current_chunk_items_ {1U};
    u32 current_helper_count_ {0U};
    std::atomic<usize> next_item_ {0U};
    u64 generation_ {0U};
    usize completed_workers_ {0U};
    bool stopping_ {false};
};

[[nodiscard]] ParallelExecutor& shared_compute_executor() noexcept;

} // namespace phoneme::runtime
