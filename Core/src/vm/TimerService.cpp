#include "phoneme/vm/TimerService.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm {

TimerService::TimerService(Machine& machine) noexcept : machine_(machine) {}

TimerService::~TimerService() { shutdown(); }

i64 TimerService::current_time_millis() noexcept {
    const auto value = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (value > std::numeric_limits<i64>::max()) {
        return std::numeric_limits<i64>::max();
    }
    if (value < std::numeric_limits<i64>::min()) {
        return std::numeric_limits<i64>::min();
    }
    return static_cast<i64>(value);
}

void TimerService::sort_entries(std::vector<ScheduledEntry>& entries) {
    std::sort(entries.begin(), entries.end(),
              [](const ScheduledEntry& left,
                 const ScheduledEntry& right) {
                  if (left.next_time_millis != right.next_time_millis) {
                      return left.next_time_millis < right.next_time_millis;
                  }
                  return left.sequence < right.sequence;
              });
}

Status TimerService::initialize_timer(ObjectRef timer, bool daemon) {
    if (timer.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Timer receiver is null");
    }

    auto thread = machine_.class_states().allocate_instance(
        machine_.heap(), "java/lang/Thread");
    if (!thread) return std::unexpected(thread.error());
    auto registered = machine_.initialize_java_thread(*thread, ObjectRef {});
    if (!registered) return registered;

    auto state = std::make_shared<TimerState>();
    state->timer = timer;
    state->thread = *thread;
    state->daemon = daemon;
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_) {
            return fail(ErrorCode::invalid_state,
                        "Timer service is shutting down");
        }
        if (timers_.contains(timer.bits)) {
            return fail_java("java/lang/IllegalStateException",
                             "Timer was initialized twice");
        }
        timers_.emplace(timer.bits, state);
    }

    auto started = machine_.scheduler().start_native_thread(
        machine_, *thread,
        [this, state](std::stop_token stop_token)
            -> Result<std::optional<ObjectRef>> {
            return run_timer(state, stop_token);
        });
    if (!started) {
        std::scoped_lock lock(mutex_);
        timers_.erase(timer.bits);
        return started;
    }
    return {};
}

Status TimerService::initialize_task(ObjectRef task) {
    if (task.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "TimerTask receiver is null");
    }
    std::scoped_lock lock(mutex_);
    if (shutting_down_) {
        return fail(ErrorCode::invalid_state,
                    "Timer service is shutting down");
    }
    auto& state = tasks_[task.bits];
    if (state.initialized) {
        return fail_java("java/lang/IllegalStateException",
                         "TimerTask was initialized twice");
    }
    state.initialized = true;
    return {};
}

Status TimerService::schedule(ObjectRef timer,
                              ObjectRef task,
                              i64 first_time_millis,
                              i64 period_millis,
                              bool fixed_rate) {
    if (timer.is_null() || task.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Timer and TimerTask must be non-null");
    }
    if (period_millis < 0) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Timer period cannot be negative");
    }

    std::scoped_lock lock(mutex_);
    const auto timer_found = timers_.find(timer.bits);
    if (timer_found == timers_.end() || timer_found->second->cancelled ||
        shutting_down_) {
        return fail_java("java/lang/IllegalStateException",
                         "Timer is cancelled or unavailable");
    }
    auto& task_state = tasks_[task.bits];
    if (!task_state.initialized) task_state.initialized = true;
    if (task_state.scheduled || task_state.cancelled || task_state.completed) {
        return fail_java("java/lang/IllegalStateException",
                         "TimerTask is already scheduled or cancelled");
    }
    if (next_sequence_ == 0U) {
        return fail(ErrorCode::overflow,
                    "Timer schedule sequence is exhausted");
    }

    task_state.scheduled = true;
    task_state.owner_timer_bits = timer.bits;
    timer_found->second->entries.push_back(ScheduledEntry {
        .task = task,
        .next_time_millis = first_time_millis,
        .period_millis = period_millis,
        .fixed_rate = fixed_rate,
        .sequence = next_sequence_++,
    });
    sort_entries(timer_found->second->entries);
    condition_.notify_all();
    return {};
}

Status TimerService::cancel_timer(ObjectRef timer) {
    if (timer.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Timer receiver is null");
    }
    std::scoped_lock lock(mutex_);
    const auto found = timers_.find(timer.bits);
    if (found == timers_.end()) return {};
    found->second->cancelled = true;
    found->second->entries.clear();
    condition_.notify_all();
    return {};
}

Result<bool> TimerService::cancel_task(ObjectRef task) {
    if (task.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "TimerTask receiver is null");
    }
    std::scoped_lock lock(mutex_);
    auto& state = tasks_[task.bits];
    if (!state.initialized) state.initialized = true;
    const bool prevented = state.scheduled && !state.cancelled &&
                           !state.completed;
    state.cancelled = true;
    if (state.owner_timer_bits != 0U) {
        const auto owner = timers_.find(state.owner_timer_bits);
        if (owner != timers_.end()) {
            std::erase_if(owner->second->entries,
                          [task](const ScheduledEntry& entry) {
                              return entry.task == task;
                          });
        }
    }
    condition_.notify_all();
    return prevented;
}

Result<i64> TimerService::scheduled_execution_time(ObjectRef task) const {
    if (task.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "TimerTask receiver is null");
    }
    std::scoped_lock lock(mutex_);
    const auto found = tasks_.find(task.bits);
    return found == tasks_.end() ? 0 : found->second.scheduled_execution_time;
}

Result<std::optional<ObjectRef>> TimerService::run_timer(
    const std::shared_ptr<TimerState>& timer,
    std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        ScheduledEntry entry;
        bool has_entry = false;
        {
            std::unique_lock lock(mutex_);
            for (;;) {
                if (shutting_down_ || timer->cancelled ||
                    stop_token.stop_requested()) {
                    return std::optional<ObjectRef> {};
                }

                std::erase_if(timer->entries,
                              [this](const ScheduledEntry& candidate) {
                                  const auto task = tasks_.find(candidate.task.bits);
                                  return task == tasks_.end() ||
                                         task->second.cancelled ||
                                         task->second.completed;
                              });
                sort_entries(timer->entries);
                if (timer->entries.empty()) {
                    machine_.scheduler().set_current_state(
                        JavaThreadState::waiting);
                    condition_.wait(lock, [&] {
                        return shutting_down_ || timer->cancelled ||
                               stop_token.stop_requested() ||
                               !timer->entries.empty();
                    });
                    machine_.scheduler().set_current_state(
                        JavaThreadState::running);
                    continue;
                }

                const i64 now = current_time_millis();
                const i64 due = timer->entries.front().next_time_millis;
                if (due > now) {
                    const auto deadline = std::chrono::system_clock::time_point(
                        std::chrono::milliseconds(due));
                    machine_.scheduler().set_current_state(
                        JavaThreadState::sleeping);
                    condition_.wait_until(lock, deadline);
                    machine_.scheduler().set_current_state(
                        JavaThreadState::running);
                    continue;
                }

                entry = timer->entries.front();
                timer->entries.erase(timer->entries.begin());
                auto task = tasks_.find(entry.task.bits);
                if (task == tasks_.end() || task->second.cancelled) continue;
                task->second.scheduled_execution_time = entry.next_time_millis;
                if (entry.period_millis == 0) {
                    task->second.completed = true;
                } else if (entry.fixed_rate) {
                    if (entry.next_time_millis >
                        std::numeric_limits<i64>::max() - entry.period_millis) {
                        task->second.completed = true;
                    } else {
                        entry.next_time_millis += entry.period_millis;
                        timer->entries.push_back(entry);
                        sort_entries(timer->entries);
                    }
                }
                has_entry = true;
                break;
            }
        }

        if (!has_entry) continue;
        auto invoked = machine_.invoke_instance(
            entry.task,
            "java/util/TimerTask",
            "run",
            "()V");
        if (!invoked) {
            std::scoped_lock lock(mutex_);
            timer->cancelled = true;
            timer->entries.clear();
            return std::unexpected(invoked.error());
        }
        if (invoked->throwable.has_value()) {
            std::scoped_lock lock(mutex_);
            timer->cancelled = true;
            timer->entries.clear();
            return invoked->throwable;
        }

        if (entry.period_millis > 0 && !entry.fixed_rate) {
            std::scoped_lock lock(mutex_);
            const auto task = tasks_.find(entry.task.bits);
            if (!timer->cancelled && task != tasks_.end() &&
                !task->second.cancelled && !task->second.completed &&
                !shutting_down_) {
                const i64 now = current_time_millis();
                if (now <= std::numeric_limits<i64>::max() -
                               entry.period_millis) {
                    entry.next_time_millis = now + entry.period_millis;
                    timer->entries.push_back(entry);
                    sort_entries(timer->entries);
                    condition_.notify_all();
                } else {
                    task->second.completed = true;
                }
            }
        }
    }
    return std::optional<ObjectRef> {};
}

void TimerService::append_reference_roots(
    std::vector<ObjectRef>& roots) const {
    std::scoped_lock lock(mutex_);
    roots.reserve(roots.size() + timers_.size() * 2U + tasks_.size());
    for (const auto& [bits, timer] : timers_) {
        (void)bits;
        if (!timer->timer.is_null()) roots.push_back(timer->timer);
        if (!timer->thread.is_null()) roots.push_back(timer->thread);
        for (const ScheduledEntry& entry : timer->entries) {
            if (!entry.task.is_null()) roots.push_back(entry.task);
        }
    }
    for (const auto& [bits, state] : tasks_) {
        if (state.initialized && !state.completed) {
            roots.push_back(ObjectRef {bits});
        }
    }
}

void TimerService::shutdown() noexcept {
    std::scoped_lock lock(mutex_);
    if (shutting_down_) return;
    shutting_down_ = true;
    for (auto& [bits, timer] : timers_) {
        (void)bits;
        timer->cancelled = true;
        timer->entries.clear();
    }
    condition_.notify_all();
}

} // namespace phoneme::vm
