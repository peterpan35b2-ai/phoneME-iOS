#include "phoneme/vm/Scheduler.hpp"

#include <algorithm>
#include <thread>

#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm {

thread_local Scheduler* Scheduler::tls_scheduler_ = nullptr;
thread_local JavaThreadId Scheduler::tls_thread_id_ = 0;

Scheduler::Scheduler() {
    auto main_thread = std::make_shared<JavaThread>(
        1U, ObjectRef {}, ObjectRef {});
    main_thread->started_ = true;
    main_thread->state_ = JavaThreadState::running;
    by_id_.emplace(1U, main_thread);
    tls_scheduler_ = this;
    tls_thread_id_ = 1U;
}

Scheduler::~Scheduler() {
    shutdown();
    if (tls_scheduler_ == this) {
        tls_scheduler_ = nullptr;
        tls_thread_id_ = 0;
    }
}

JavaThreadId Scheduler::current_thread_id() const noexcept {
    if (tls_scheduler_ == this && tls_thread_id_ != 0U) {
        return tls_thread_id_;
    }
    return 1U;
}

std::shared_ptr<JavaThread> Scheduler::find_thread_locked(
    ObjectRef thread_object) const {
    if (thread_object.is_null()) {
        return {};
    }
    const auto iterator = by_object_.find(thread_object.bits);
    return iterator == by_object_.end() ? nullptr : iterator->second;
}

std::shared_ptr<JavaThread> Scheduler::current_thread_record() const {
    const JavaThreadId id = current_thread_id();
    std::scoped_lock lock(mutex_);
    const auto iterator = by_id_.find(id);
    return iterator == by_id_.end() ? nullptr : iterator->second;
}

Result<ObjectRef> Scheduler::current_thread_object() const {
    auto thread = current_thread_record();
    if (!thread) {
        return fail(ErrorCode::invalid_state,
                    "current Java thread is not registered");
    }
    std::scoped_lock lock(thread->mutex_);
    if (thread->object_.is_null()) {
        return fail(ErrorCode::invalid_state,
                    "current Java thread has no java.lang.Thread object");
    }
    return thread->object_;
}

Status Scheduler::bind_current_thread_object(ObjectRef object) {
    if (object.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot bind a null java.lang.Thread object");
    }
    auto thread = current_thread_record();
    if (!thread) {
        return fail(ErrorCode::invalid_state,
                    "current Java thread is not registered");
    }
    std::scoped_lock scheduler_lock(mutex_);
    const auto existing = by_object_.find(object.bits);
    if (existing != by_object_.end() && existing->second != thread) {
        return fail(ErrorCode::invalid_state,
                    "java.lang.Thread object is already bound");
    }
    {
        std::scoped_lock thread_lock(thread->mutex_);
        if (!thread->object_.is_null() && thread->object_ != object) {
            by_object_.erase(thread->object_.bits);
        }
        thread->object_ = object;
    }
    by_object_[object.bits] = thread;
    return {};
}

Status Scheduler::register_thread(ObjectRef thread_object,
                                  ObjectRef runnable_target) {
    if (thread_object.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot register a null java.lang.Thread object");
    }
    std::scoped_lock lock(mutex_);
    if (shutting_down_) {
        return fail(ErrorCode::invalid_state,
                    "scheduler is shutting down");
    }
    if (auto existing = find_thread_locked(thread_object)) {
        std::scoped_lock thread_lock(existing->mutex_);
        if (existing->started_) {
            return fail_java("java/lang/IllegalThreadStateException",
                             "Thread constructor called after start");
        }
        existing->target_ = runnable_target;
        return {};
    }
    if (next_thread_id_ == 0U) {
        return fail(ErrorCode::overflow,
                    "Java thread identifier space was exhausted");
    }
    const JavaThreadId id = next_thread_id_++;
    auto thread = std::make_shared<JavaThread>(id,
                                               thread_object,
                                               runnable_target);
    by_object_.emplace(thread_object.bits, thread);
    by_id_.emplace(id, std::move(thread));
    return {};
}

Status Scheduler::start_thread(Machine& machine, ObjectRef thread_object) {
    std::shared_ptr<JavaThread> thread;
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_) {
            return fail(ErrorCode::invalid_state,
                        "scheduler is shutting down");
        }
        thread = find_thread_locked(thread_object);
        if (!thread) {
            if (next_thread_id_ == 0U) {
                return fail(ErrorCode::overflow,
                            "Java thread identifier space was exhausted");
            }
            const JavaThreadId id = next_thread_id_++;
            thread = std::make_shared<JavaThread>(
                id, thread_object, ObjectRef {});
            by_object_.emplace(thread_object.bits, thread);
            by_id_.emplace(id, thread);
        }
        {
            std::scoped_lock thread_lock(thread->mutex_);
            if (thread->started_) {
                return fail_java("java/lang/IllegalThreadStateException",
                                 "Thread.start called more than once");
            }
            thread->started_ = true;
            thread->state_ = JavaThreadState::runnable;
        }
        update_queue_membership_locked(thread->id_,
                                       JavaThreadState::runnable);
    }

    thread->worker_ = std::jthread(
        [this, &machine, thread](std::stop_token stop_token) {
            tls_scheduler_ = this;
            tls_thread_id_ = thread->id_;

            std::optional<ObjectRef> throwable;
            std::optional<Error> failure;
            if (!stop_token.stop_requested()) {
                auto result = machine.invoke_instance(thread->object_,
                                                      "java/lang/Thread",
                                                      "run",
                                                      "()V");
                if (!result) {
                    failure = result.error();
                } else if (result->throwable.has_value()) {
                    throwable = result->throwable;
                }
            }

            machine.monitors().release_all(thread->id_);
            finish_thread(thread, throwable, failure);
            tls_scheduler_ = nullptr;
            tls_thread_id_ = 0;
        });
    return {};
}

Status Scheduler::start_native_thread(
    Machine& machine,
    ObjectRef thread_object,
    SchedulerNativeTask task) {
    if (!task) {
        return fail(ErrorCode::invalid_argument,
                    "native Java thread requires a task");
    }
    std::shared_ptr<JavaThread> thread;
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_) {
            return fail(ErrorCode::invalid_state,
                        "scheduler is shutting down");
        }
        thread = find_thread_locked(thread_object);
        if (!thread) {
            if (next_thread_id_ == 0U) {
                return fail(ErrorCode::overflow,
                            "Java thread identifier space was exhausted");
            }
            const JavaThreadId id = next_thread_id_++;
            thread = std::make_shared<JavaThread>(
                id, thread_object, ObjectRef {});
            by_object_.emplace(thread_object.bits, thread);
            by_id_.emplace(id, thread);
        }
        {
            std::scoped_lock thread_lock(thread->mutex_);
            if (thread->started_) {
                return fail_java("java/lang/IllegalThreadStateException",
                                 "native Java thread was started twice");
            }
            thread->started_ = true;
            thread->state_ = JavaThreadState::runnable;
        }
        update_queue_membership_locked(thread->id_,
                                       JavaThreadState::runnable);
    }

    thread->worker_ = std::jthread(
        [this, &machine, thread, task = std::move(task)](
            std::stop_token stop_token) mutable {
            tls_scheduler_ = this;
            tls_thread_id_ = thread->id_;
            set_current_state(JavaThreadState::running);

            std::optional<ObjectRef> throwable;
            std::optional<Error> failure;
            if (!stop_token.stop_requested()) {
                auto result = task(stop_token);
                if (!result) {
                    failure = result.error();
                } else {
                    throwable = *result;
                }
            }

            machine.monitors().release_all(thread->id_);
            finish_thread(thread, throwable, failure);
            tls_scheduler_ = nullptr;
            tls_thread_id_ = 0;
        });
    return {};
}

Result<ObjectRef> Scheduler::runnable_target(ObjectRef thread_object) const {
    std::shared_ptr<JavaThread> thread;
    {
        std::scoped_lock lock(mutex_);
        thread = find_thread_locked(thread_object);
    }
    if (!thread) {
        return fail(ErrorCode::invalid_state,
                    "java.lang.Thread object is not registered");
    }
    std::scoped_lock lock(thread->mutex_);
    return thread->target_;
}

void Scheduler::cooperative_yield(Machine& machine) {
    auto current = current_thread_record();
    if (!current) {
        return;
    }
    set_current_state(JavaThreadState::runnable);
    const u32 depth = machine.suspend_execution_for_blocking();
    if (deterministic()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } else {
        std::this_thread::yield();
    }
    machine.resume_execution_after_blocking(depth);
    set_current_state(JavaThreadState::running);
}

Result<SchedulerWaitResult> Scheduler::sleep_current(
    Machine& machine,
    std::chrono::milliseconds duration) {
    auto current = current_thread_record();
    if (!current) {
        return fail(ErrorCode::invalid_state,
                    "current Java thread is not registered");
    }
    if (duration.count() == 0) {
        cooperative_yield(machine);
        return SchedulerWaitResult::completed;
    }

    {
        std::scoped_lock lock(current->mutex_);
        if (current->interrupted_) {
            current->interrupted_ = false;
            return SchedulerWaitResult::interrupted;
        }
    }

    set_current_state(JavaThreadState::sleeping);
    const u32 depth = machine.suspend_execution_for_blocking();
    bool interrupted = false;
    {
        std::unique_lock lock(current->mutex_);
        current->condition_.wait_for(lock, duration, [&current] {
            return current->interrupted_ || current->stop_requested_;
        });
        interrupted = current->interrupted_;
        if (interrupted) {
            current->interrupted_ = false;
        }
    }
    machine.resume_execution_after_blocking(depth);
    set_current_state(JavaThreadState::running);
    return interrupted ? SchedulerWaitResult::interrupted
                       : SchedulerWaitResult::completed;
}

Result<SchedulerWaitResult> Scheduler::join_current(
    Machine& machine,
    ObjectRef target,
    std::optional<std::chrono::milliseconds> timeout) {
    std::shared_ptr<JavaThread> target_thread;
    {
        std::scoped_lock lock(mutex_);
        target_thread = find_thread_locked(target);
    }
    if (!target_thread) {
        return SchedulerWaitResult::completed;
    }
    if (target_thread->id_ == current_thread_id()) {
        if (timeout.has_value() && timeout->count() == 0) {
            return SchedulerWaitResult::timed_out;
        }
        return fail(ErrorCode::invalid_argument,
                    "a Java thread cannot join itself indefinitely");
    }

    auto current = current_thread_record();
    if (!current) {
        return fail(ErrorCode::invalid_state,
                    "current Java thread is not registered");
    }
    {
        std::scoped_lock lock(current->mutex_);
        if (current->interrupted_) {
            current->interrupted_ = false;
            return SchedulerWaitResult::interrupted;
        }
    }

    {
        std::scoped_lock lock(target_thread->mutex_);
        if (!target_thread->started_ ||
            target_thread->state_ == JavaThreadState::terminated) {
            return SchedulerWaitResult::completed;
        }
    }

    set_current_state(JavaThreadState::joining);
    const u32 depth = machine.suspend_execution_for_blocking();
    bool completed = false;
    bool interrupted = false;
    {
        std::unique_lock lock(target_thread->mutex_);
        const auto predicate = [&] {
            std::scoped_lock current_lock(current->mutex_);
            interrupted = current->interrupted_;
            return target_thread->state_ == JavaThreadState::terminated ||
                   interrupted || current->stop_requested_;
        };
        if (timeout.has_value()) {
            completed = target_thread->condition_.wait_for(
                lock, *timeout, predicate);
        } else {
            target_thread->condition_.wait(lock, predicate);
            completed = true;
        }
        if (target_thread->state_ == JavaThreadState::terminated) {
            completed = true;
        }
    }
    if (interrupted) {
        std::scoped_lock lock(current->mutex_);
        current->interrupted_ = false;
    }
    machine.resume_execution_after_blocking(depth);
    set_current_state(JavaThreadState::running);

    if (interrupted) {
        return SchedulerWaitResult::interrupted;
    }
    return completed ? SchedulerWaitResult::completed
                     : SchedulerWaitResult::timed_out;
}

Status Scheduler::interrupt(ObjectRef thread_object) {
    std::shared_ptr<JavaThread> thread;
    std::vector<std::shared_ptr<JavaThread>> wake_targets;
    {
        std::scoped_lock lock(mutex_);
        thread = find_thread_locked(thread_object);
        wake_targets.reserve(by_id_.size());
        for (const auto& [id, candidate] : by_id_) {
            (void)id;
            wake_targets.push_back(candidate);
        }
    }
    if (!thread) {
        return {};
    }
    {
        std::scoped_lock lock(thread->mutex_);
        thread->interrupted_ = true;
    }
    // A joining thread waits on the target's condition variable, not its own.
    // Wake every scheduler condition so interruption is observed immediately
    // regardless of the current blocking reason.
    for (const auto& candidate : wake_targets) {
        candidate->condition_.notify_all();
    }
    return {};
}

bool Scheduler::current_is_interrupted() const noexcept {
    auto current = current_thread_record();
    if (!current) {
        return false;
    }
    std::scoped_lock lock(current->mutex_);
    return current->interrupted_;
}

bool Scheduler::current_stop_requested() const noexcept {
    auto current = current_thread_record();
    if (!current) {
        return false;
    }
    std::scoped_lock lock(current->mutex_);
    return current->stop_requested_;
}

bool Scheduler::consume_current_interrupt() noexcept {
    auto current = current_thread_record();
    if (!current) {
        return false;
    }
    std::scoped_lock lock(current->mutex_);
    const bool interrupted = current->interrupted_;
    current->interrupted_ = false;
    return interrupted;
}

Result<bool> Scheduler::is_interrupted(ObjectRef thread_object) const {
    std::shared_ptr<JavaThread> thread;
    {
        std::scoped_lock lock(mutex_);
        thread = find_thread_locked(thread_object);
    }
    if (!thread) {
        return false;
    }
    std::scoped_lock lock(thread->mutex_);
    return thread->interrupted_;
}

Result<bool> Scheduler::is_alive(ObjectRef thread_object) const {
    std::shared_ptr<JavaThread> thread;
    {
        std::scoped_lock lock(mutex_);
        thread = find_thread_locked(thread_object);
    }
    if (!thread) {
        return false;
    }
    std::scoped_lock lock(thread->mutex_);
    return thread->started_ &&
           thread->state_ != JavaThreadState::terminated;
}

Status Scheduler::set_priority(ObjectRef thread_object, i32 priority) {
    if (priority < 1 || priority > 10) {
        return fail_java("java/lang/IllegalArgumentException",
                         "Thread priority must be in the range 1..10");
    }
    std::shared_ptr<JavaThread> thread;
    {
        std::scoped_lock lock(mutex_);
        thread = find_thread_locked(thread_object);
    }
    if (!thread) {
        return fail(ErrorCode::invalid_state,
                    "java.lang.Thread object is not registered");
    }
    std::scoped_lock lock(thread->mutex_);
    thread->priority_ = priority;
    return {};
}

Result<i32> Scheduler::priority(ObjectRef thread_object) const {
    std::shared_ptr<JavaThread> thread;
    {
        std::scoped_lock lock(mutex_);
        thread = find_thread_locked(thread_object);
    }
    if (!thread) {
        return 5;
    }
    std::scoped_lock lock(thread->mutex_);
    return thread->priority_;
}

void Scheduler::erase_id(std::deque<JavaThreadId>& queue,
                         JavaThreadId id) noexcept {
    queue.erase(std::remove(queue.begin(), queue.end(), id), queue.end());
}

void Scheduler::update_queue_membership_locked(JavaThreadId id,
                                                JavaThreadState state) {
    erase_id(runnable_queue_, id);
    erase_id(blocked_queue_, id);
    erase_id(sleeping_queue_, id);
    switch (state) {
    case JavaThreadState::runnable:
    case JavaThreadState::running:
        runnable_queue_.push_back(id);
        break;
    case JavaThreadState::blocked_monitor:
    case JavaThreadState::blocked_io:
    case JavaThreadState::waiting:
    case JavaThreadState::joining:
        blocked_queue_.push_back(id);
        break;
    case JavaThreadState::sleeping:
        sleeping_queue_.push_back(id);
        break;
    case JavaThreadState::new_thread:
    case JavaThreadState::terminated:
        break;
    }
}

void Scheduler::set_current_state(JavaThreadState state) noexcept {
    auto current = current_thread_record();
    if (!current) {
        return;
    }
    {
        std::scoped_lock lock(current->mutex_);
        current->state_ = state;
    }
    std::scoped_lock lock(mutex_);
    update_queue_membership_locked(current->id_, state);
}

void Scheduler::publish_current_roots(
    u32 invocation_depth,
    std::span<const ObjectRef> roots) {
    auto current = current_thread_record();
    if (current) {
        current->context_->publish_roots(invocation_depth, roots);
    }
}

void Scheduler::clear_current_roots(u32 invocation_depth) noexcept {
    auto current = current_thread_record();
    if (current) {
        current->context_->clear_roots(invocation_depth);
    }
}

void Scheduler::set_current_pending_exception(
    std::optional<ObjectRef> throwable) noexcept {
    auto current = current_thread_record();
    if (current) {
        current->context_->set_pending_exception(throwable);
    }
}

void Scheduler::add_current_executed_instructions(u64 count) noexcept {
    auto current = current_thread_record();
    if (current) {
        current->context_->add_executed_instructions(count);
    }
}

void Scheduler::append_reference_roots(std::vector<ObjectRef>& roots) const {
    std::vector<std::shared_ptr<JavaThread>> threads;
    {
        std::scoped_lock lock(mutex_);
        threads.reserve(by_id_.size());
        for (const auto& [id, thread] : by_id_) {
            (void)id;
            threads.push_back(thread);
        }
    }
    for (const auto& thread : threads) {
        std::scoped_lock lock(thread->mutex_);
        if (!thread->object_.is_null()) {
            roots.push_back(thread->object_);
        }
        if (!thread->target_.is_null()) {
            roots.push_back(thread->target_);
        }
        if (thread->uncaught_throwable_.has_value() &&
            !thread->uncaught_throwable_->is_null()) {
            roots.push_back(*thread->uncaught_throwable_);
        }
        thread->context_->append_reference_roots(roots);
    }
}

SchedulerSnapshot Scheduler::snapshot() const {
    SchedulerSnapshot result;
    std::scoped_lock lock(mutex_);
    result.runnable.assign(runnable_queue_.begin(), runnable_queue_.end());
    result.blocked.assign(blocked_queue_.begin(), blocked_queue_.end());
    result.sleeping.assign(sleeping_queue_.begin(), sleeping_queue_.end());
    result.threads.reserve(by_id_.size());
    for (const auto& [id, thread] : by_id_) {
        (void)id;
        result.threads.push_back(thread->snapshot());
    }
    std::sort(result.threads.begin(), result.threads.end(),
              [](const JavaThreadSnapshot& left,
                 const JavaThreadSnapshot& right) {
                  return left.id < right.id;
              });
    return result;
}

void Scheduler::set_deterministic(bool enabled) noexcept {
    std::scoped_lock lock(mutex_);
    deterministic_ = enabled;
}

bool Scheduler::deterministic() const noexcept {
    std::scoped_lock lock(mutex_);
    return deterministic_;
}

void Scheduler::wake_thread(JavaThreadId thread_id) noexcept {
    std::shared_ptr<JavaThread> thread;
    {
        std::scoped_lock lock(mutex_);
        const auto iterator = by_id_.find(thread_id);
        if (iterator != by_id_.end()) {
            thread = iterator->second;
        }
    }
    if (thread) {
        thread->condition_.notify_all();
    }
}

void Scheduler::finish_thread(const std::shared_ptr<JavaThread>& thread,
                              std::optional<ObjectRef> throwable,
                              std::optional<Error> failure) noexcept {
    {
        std::scoped_lock lock(thread->mutex_);
        thread->uncaught_throwable_ = throwable;
        thread->native_failure_ = std::move(failure);
        thread->state_ = JavaThreadState::terminated;
    }
    thread->context_->set_pending_exception(throwable);
    thread->condition_.notify_all();
    std::scoped_lock lock(mutex_);
    update_queue_membership_locked(thread->id_,
                                   JavaThreadState::terminated);
}

void Scheduler::shutdown() noexcept {
    std::vector<std::shared_ptr<JavaThread>> threads;
    {
        std::scoped_lock lock(mutex_);
        if (shutting_down_) {
            return;
        }
        shutting_down_ = true;
        for (const auto& [id, thread] : by_id_) {
            if (id != 1U) {
                threads.push_back(thread);
            }
        }
    }
    for (const auto& thread : threads) {
        {
            std::scoped_lock lock(thread->mutex_);
            thread->stop_requested_ = true;
            thread->interrupted_ = true;
        }
        thread->worker_.request_stop();
        thread->condition_.notify_all();
    }
    for (const auto& thread : threads) {
        if (thread->worker_.joinable() &&
            thread->worker_.get_id() != std::this_thread::get_id()) {
            thread->worker_.join();
        }
    }
}

} // namespace phoneme::vm
