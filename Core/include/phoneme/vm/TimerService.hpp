#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <unordered_map>
#include <vector>

#include "phoneme/base/Error.hpp"
#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;

class TimerService final {
public:
    explicit TimerService(Machine& machine) noexcept;
    ~TimerService();

    TimerService(const TimerService&) = delete;
    TimerService& operator=(const TimerService&) = delete;

    [[nodiscard]] Status initialize_timer(ObjectRef timer, bool daemon);
    [[nodiscard]] Status initialize_task(ObjectRef task);
    [[nodiscard]] Status schedule(ObjectRef timer,
                                  ObjectRef task,
                                  i64 first_time_millis,
                                  i64 period_millis,
                                  bool fixed_rate);
    [[nodiscard]] Status cancel_timer(ObjectRef timer);
    [[nodiscard]] Result<bool> cancel_task(ObjectRef task);
    [[nodiscard]] Result<i64> scheduled_execution_time(ObjectRef task) const;

    void append_reference_roots(std::vector<ObjectRef>& roots) const;
    void shutdown() noexcept;

private:
    struct ScheduledEntry final {
        ObjectRef task;
        i64 next_time_millis {0};
        i64 period_millis {0};
        bool fixed_rate {false};
        u64 sequence {0};
    };

    struct TimerState final {
        ObjectRef timer;
        ObjectRef thread;
        bool daemon {false};
        bool cancelled {false};
        std::vector<ScheduledEntry> entries;
    };

    struct TaskState final {
        bool initialized {false};
        bool scheduled {false};
        bool cancelled {false};
        bool completed {false};
        i64 scheduled_execution_time {0};
        u64 owner_timer_bits {0};
    };

    [[nodiscard]] Result<std::optional<ObjectRef>> run_timer(
        const std::shared_ptr<TimerState>& timer,
        std::stop_token stop_token);
    [[nodiscard]] static i64 current_time_millis() noexcept;
    static void sort_entries(std::vector<ScheduledEntry>& entries);

    Machine& machine_;
    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    std::unordered_map<u64, std::shared_ptr<TimerState>> timers_;
    std::unordered_map<u64, TaskState> tasks_;
    u64 next_sequence_ {1};
    bool shutting_down_ {false};
};

} // namespace phoneme::vm
