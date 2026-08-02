#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include "phoneme/vm/JavaThread.hpp"

namespace phoneme::vm {

class Machine;
class MonitorTable;

enum class SchedulerWaitResult : u8 {
    completed,
    timed_out,
    interrupted,
};

struct SchedulerSnapshot final {
    std::vector<JavaThreadSnapshot> threads;
    std::vector<JavaThreadId> runnable;
    std::vector<JavaThreadId> blocked;
    std::vector<JavaThreadId> sleeping;
};

class Scheduler final {
public:
    Scheduler();
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    [[nodiscard]] JavaThreadId current_thread_id() const noexcept;
    [[nodiscard]] Result<ObjectRef> current_thread_object() const;
    [[nodiscard]] Status bind_current_thread_object(ObjectRef object);

    [[nodiscard]] Status register_thread(ObjectRef thread_object,
                                         ObjectRef runnable_target);
    [[nodiscard]] Status start_thread(Machine& machine,
                                      ObjectRef thread_object);
    [[nodiscard]] Result<ObjectRef> runnable_target(
        ObjectRef thread_object) const;

    void cooperative_yield(Machine& machine);
    [[nodiscard]] Result<SchedulerWaitResult> sleep_current(
        Machine& machine,
        std::chrono::milliseconds duration);
    [[nodiscard]] Result<SchedulerWaitResult> join_current(
        Machine& machine,
        ObjectRef target,
        std::optional<std::chrono::milliseconds> timeout);

    [[nodiscard]] Status interrupt(ObjectRef thread_object);
    [[nodiscard]] bool current_is_interrupted() const noexcept;
    [[nodiscard]] bool consume_current_interrupt() noexcept;
    [[nodiscard]] Result<bool> is_interrupted(ObjectRef thread_object) const;
    [[nodiscard]] Result<bool> is_alive(ObjectRef thread_object) const;
    [[nodiscard]] Status set_priority(ObjectRef thread_object, i32 priority);
    [[nodiscard]] Result<i32> priority(ObjectRef thread_object) const;

    void set_current_state(JavaThreadState state) noexcept;
    void publish_current_roots(u32 invocation_depth,
                               std::span<const ObjectRef> roots);
    void clear_current_roots(u32 invocation_depth) noexcept;
    void set_current_pending_exception(
        std::optional<ObjectRef> throwable) noexcept;
    void add_current_executed_instructions(u64 count) noexcept;
    void append_reference_roots(std::vector<ObjectRef>& roots) const;

    [[nodiscard]] SchedulerSnapshot snapshot() const;
    void set_deterministic(bool enabled) noexcept;
    [[nodiscard]] bool deterministic() const noexcept;

    void wake_thread(JavaThreadId thread_id) noexcept;
    void shutdown() noexcept;

private:
    [[nodiscard]] std::shared_ptr<JavaThread> find_thread_locked(
        ObjectRef thread_object) const;
    [[nodiscard]] std::shared_ptr<JavaThread> current_thread_record() const;
    void finish_thread(const std::shared_ptr<JavaThread>& thread,
                       std::optional<ObjectRef> throwable,
                       std::optional<Error> failure) noexcept;
    void update_queue_membership_locked(JavaThreadId id,
                                        JavaThreadState state);
    static void erase_id(std::deque<JavaThreadId>& queue,
                         JavaThreadId id) noexcept;

    mutable std::mutex mutex_;
    std::unordered_map<u64, std::shared_ptr<JavaThread>> by_object_;
    std::unordered_map<JavaThreadId, std::shared_ptr<JavaThread>> by_id_;
    std::deque<JavaThreadId> runnable_queue_;
    std::deque<JavaThreadId> blocked_queue_;
    std::deque<JavaThreadId> sleeping_queue_;
    JavaThreadId next_thread_id_ {2};
    bool deterministic_ {false};
    bool shutting_down_ {false};

    static thread_local Scheduler* tls_scheduler_;
    static thread_local JavaThreadId tls_thread_id_;
};

} // namespace phoneme::vm
