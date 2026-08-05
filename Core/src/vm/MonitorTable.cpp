#include "phoneme/vm/MonitorTable.hpp"
#include "phoneme/vm/VmTrace.hpp"

#include <algorithm>
#include <limits>

namespace phoneme::vm {

std::shared_ptr<MonitorTable::Monitor> MonitorTable::monitor_locked(
    ObjectRef object) {
    auto& monitor = monitors_[object.bits];
    if (!monitor) {
        monitor = std::make_shared<Monitor>();
        monitor->cancelled = cancelled_;
    }
    return monitor;
}

void MonitorTable::enqueue_unique(std::deque<JavaThreadId>& queue,
                                  JavaThreadId thread_id) {
    if (std::find(queue.begin(), queue.end(), thread_id) == queue.end()) {
        queue.push_back(thread_id);
    }
}

void MonitorTable::erase_thread(std::deque<JavaThreadId>& queue,
                                JavaThreadId thread_id) noexcept {
    queue.erase(std::remove(queue.begin(), queue.end(), thread_id),
                queue.end());
}

u32 MonitorTable::waiter_count(const Monitor& monitor) noexcept {
    const usize count = monitor.entry_queue.size() + monitor.wait_set.size();
    return count > static_cast<usize>(std::numeric_limits<u32>::max())
               ? std::numeric_limits<u32>::max()
               : static_cast<u32>(count);
}

void MonitorTable::erase_if_unused_locked(
    u64 object_bits,
    const std::shared_ptr<Monitor>& monitor) {
    if (monitor->owner == 0U && monitor->entry_queue.empty() &&
        monitor->wait_set.empty()) {
        const auto iterator = monitors_.find(object_bits);
        if (iterator != monitors_.end() && iterator->second == monitor) {
            monitors_.erase(iterator);
        }
    }
}

Result<MonitorEnterResult> MonitorTable::enter(ObjectRef object,
                                               JavaThreadId thread_id) {
    if (object.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot enter monitor for null reference");
    }
    if (thread_id == 0U) {
        return fail(ErrorCode::invalid_argument,
                    "Java thread identifier must be non-zero");
    }

    std::scoped_lock lock(mutex_);
    auto monitor = monitor_locked(object);
    if (monitor->cancelled) {
        return fail(ErrorCode::invalid_state, "monitor was cancelled");
    }
    if (monitor->owner == 0U &&
        (monitor->entry_queue.empty() ||
         monitor->entry_queue.front() == thread_id)) {
        erase_thread(monitor->entry_queue, thread_id);
        monitor->owner = thread_id;
        monitor->recursion = 1U;
        return MonitorEnterResult::acquired;
    }
    if (monitor->owner == thread_id) {
        if (monitor->recursion == std::numeric_limits<u32>::max()) {
            return fail(ErrorCode::overflow,
                        "monitor recursion count overflowed");
        }
        ++monitor->recursion;
        return MonitorEnterResult::acquired;
    }
    enqueue_unique(monitor->entry_queue, thread_id);
    return MonitorEnterResult::would_block;
}

Status MonitorTable::enter_blocking(ObjectRef object,
                                    JavaThreadId thread_id,
                                    BlockHook before_block,
                                    BlockHook after_block) {
    if (object.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot enter monitor for null reference");
    }
    if (!before_block || !after_block) {
        return fail(ErrorCode::invalid_argument,
                    "monitor blocking hooks must be provided");
    }

    std::unique_lock lock(mutex_);
    auto monitor = monitor_locked(object);
    if (monitor->owner == thread_id) {
        if (monitor->recursion == std::numeric_limits<u32>::max()) {
            return fail(ErrorCode::overflow,
                        "monitor recursion count overflowed");
        }
        ++monitor->recursion;
        return {};
    }
    enqueue_unique(monitor->entry_queue, thread_id);
    const JavaThreadId blocking_owner = monitor->owner;
    const auto blocking_started = std::chrono::steady_clock::now();
    vm_trace("monitor",
             "block-enter java=%u object=%llu owner=%u queue=%zu",
             static_cast<unsigned>(thread_id),
             static_cast<unsigned long long>(object.bits),
             static_cast<unsigned>(blocking_owner),
             monitor->entry_queue.size());
    lock.unlock();
    before_block();
    lock.lock();
    monitor->condition.wait(lock, [&] {
        return monitor->cancelled ||
               (monitor->owner == 0U &&
                !monitor->entry_queue.empty() &&
                monitor->entry_queue.front() == thread_id);
    });
    if (monitor->cancelled) {
        erase_thread(monitor->entry_queue, thread_id);
        lock.unlock();
        after_block();
        return fail(ErrorCode::invalid_state,
                    "monitor wait was cancelled");
    }
    monitor->entry_queue.pop_front();
    monitor->owner = thread_id;
    monitor->recursion = 1U;
    lock.unlock();
    after_block();
    const auto blocked_for =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - blocking_started);
    vm_trace("monitor",
             "acquired java=%u object=%llu waited_us=%lld",
             static_cast<unsigned>(thread_id),
             static_cast<unsigned long long>(object.bits),
             static_cast<long long>(blocked_for.count()));
    return {};
}

Status MonitorTable::exit(ObjectRef object, JavaThreadId thread_id) {
    if (object.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot exit monitor for null reference");
    }
    std::unique_lock lock(mutex_);
    const auto iterator = monitors_.find(object.bits);
    if (iterator == monitors_.end() || iterator->second->owner != thread_id ||
        iterator->second->recursion == 0U) {
        return fail_java("java/lang/IllegalMonitorStateException",
                         "current Java thread does not own monitor");
    }
    auto monitor = iterator->second;
    --monitor->recursion;
    if (monitor->recursion == 0U) {
        monitor->owner = 0U;
        monitor->condition.notify_all();
        erase_if_unused_locked(object.bits, monitor);
    }
    return {};
}

Result<MonitorWaitResult> MonitorTable::wait(
    ObjectRef object,
    JavaThreadId thread_id,
    std::optional<std::chrono::milliseconds> timeout,
    BlockHook before_block,
    BlockHook after_block,
    InterruptCheck interrupted) {
    if (object.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Object.wait receiver is null");
    }
    if (!before_block || !after_block || !interrupted) {
        return fail(ErrorCode::invalid_argument,
                    "monitor wait callbacks must be provided");
    }

    std::unique_lock lock(mutex_);
    const auto iterator = monitors_.find(object.bits);
    if (iterator == monitors_.end() || iterator->second->owner != thread_id ||
        iterator->second->recursion == 0U) {
        return fail_java("java/lang/IllegalMonitorStateException",
                         "Object.wait requires monitor ownership");
    }
    auto monitor = iterator->second;
    const u32 saved_recursion = monitor->recursion;
    const auto wait_started = std::chrono::steady_clock::now();
    auto waiter = std::make_shared<WaitNode>();
    waiter->thread_id = thread_id;
    monitor->wait_set.push_back(waiter);
    monitor->owner = 0U;
    monitor->recursion = 0U;
    monitor->condition.notify_all();
    vm_trace("monitor",
             "wait java=%u object=%llu timeout_ms=%lld recursion=%u waiters=%zu",
             static_cast<unsigned>(thread_id),
             static_cast<unsigned long long>(object.bits),
             timeout.has_value()
                 ? static_cast<long long>(timeout->count())
                 : -1LL,
             static_cast<unsigned>(saved_recursion),
             monitor->wait_set.size());

    lock.unlock();
    before_block();
    lock.lock();

    const auto predicate = [&] {
        return monitor->cancelled || waiter->notified || interrupted();
    };
    bool awakened = false;
    if (timeout.has_value()) {
        awakened = monitor->condition.wait_for(lock, *timeout, predicate);
    } else {
        monitor->condition.wait(lock, predicate);
        awakened = true;
    }

    const bool was_interrupted = interrupted();
    const bool was_notified = waiter->notified;
    monitor->wait_set.erase(
        std::remove(monitor->wait_set.begin(), monitor->wait_set.end(), waiter),
        monitor->wait_set.end());

    if (monitor->cancelled) {
        lock.unlock();
        after_block();
        return fail(ErrorCode::invalid_state,
                    "monitor wait was cancelled");
    }

    enqueue_unique(monitor->entry_queue, thread_id);
    monitor->condition.wait(lock, [&] {
        return monitor->cancelled ||
               (monitor->owner == 0U &&
                !monitor->entry_queue.empty() &&
                monitor->entry_queue.front() == thread_id);
    });
    if (monitor->cancelled) {
        erase_thread(monitor->entry_queue, thread_id);
        lock.unlock();
        after_block();
        return fail(ErrorCode::invalid_state,
                    "monitor reacquisition was cancelled");
    }
    monitor->entry_queue.pop_front();
    monitor->owner = thread_id;
    monitor->recursion = saved_recursion;
    lock.unlock();
    after_block();
    const auto waited_for =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - wait_started);
    vm_trace("monitor",
             "wait-end java=%u object=%llu result=%s waited_us=%lld",
             static_cast<unsigned>(thread_id),
             static_cast<unsigned long long>(object.bits),
             was_interrupted ? "interrupted"
                             : (was_notified ? "notified"
                                             : (awakened ? "awakened"
                                                         : "timed-out")),
             static_cast<long long>(waited_for.count()));

    if (was_interrupted) {
        return MonitorWaitResult::interrupted;
    }
    if (was_notified) {
        return MonitorWaitResult::notified;
    }
    return awakened ? MonitorWaitResult::notified
                    : MonitorWaitResult::timed_out;
}

Status MonitorTable::notify_one(ObjectRef object,
                                JavaThreadId thread_id) {
    if (object.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Object.notify receiver is null");
    }
    std::scoped_lock lock(mutex_);
    const auto iterator = monitors_.find(object.bits);
    if (iterator == monitors_.end() || iterator->second->owner != thread_id) {
        return fail_java("java/lang/IllegalMonitorStateException",
                         "Object.notify requires monitor ownership");
    }
    auto& monitor = *iterator->second;
    const auto waiter = std::find_if(
        monitor.wait_set.begin(), monitor.wait_set.end(),
        [](const std::shared_ptr<WaitNode>& node) {
            return !node->notified;
        });
    if (waiter != monitor.wait_set.end()) {
        (*waiter)->notified = true;
        monitor.condition.notify_all();
    }
    vm_trace("monitor",
             "notify java=%u object=%llu selected=%d waiters=%zu",
             static_cast<unsigned>(thread_id),
             static_cast<unsigned long long>(object.bits),
             waiter != monitor.wait_set.end() ? 1 : 0,
             monitor.wait_set.size());
    return {};
}

Status MonitorTable::notify_all(ObjectRef object,
                                JavaThreadId thread_id) {
    if (object.is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "Object.notifyAll receiver is null");
    }
    std::scoped_lock lock(mutex_);
    const auto iterator = monitors_.find(object.bits);
    if (iterator == monitors_.end() || iterator->second->owner != thread_id) {
        return fail_java("java/lang/IllegalMonitorStateException",
                         "Object.notifyAll requires monitor ownership");
    }
    auto& monitor = *iterator->second;
    for (const auto& waiter : monitor.wait_set) {
        waiter->notified = true;
    }
    monitor.condition.notify_all();
    vm_trace("monitor",
             "notify-all java=%u object=%llu waiters=%zu",
             static_cast<unsigned>(thread_id),
             static_cast<unsigned long long>(object.bits),
             monitor.wait_set.size());
    return {};
}

Result<MonitorSnapshot> MonitorTable::snapshot(ObjectRef object) const {
    if (object.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot inspect monitor for null reference");
    }
    std::scoped_lock lock(mutex_);
    const auto iterator = monitors_.find(object.bits);
    if (iterator == monitors_.end()) {
        return MonitorSnapshot {};
    }
    return MonitorSnapshot {
        .owner = iterator->second->owner,
        .recursion = iterator->second->recursion,
        .waiters = waiter_count(*iterator->second),
    };
}

void MonitorTable::append_reference_roots(std::vector<ObjectRef>& roots) const {
    std::scoped_lock lock(mutex_);
    roots.reserve(roots.size() + monitors_.size());
    for (const auto& [bits, monitor] : monitors_) {
        (void)monitor;
        roots.push_back(ObjectRef {bits});
    }
}

void MonitorTable::wake_thread(JavaThreadId thread_id) noexcept {
    std::scoped_lock lock(mutex_);
    for (const auto& [bits, monitor] : monitors_) {
        (void)bits;
        const bool present = monitor->owner == thread_id ||
            std::find(monitor->entry_queue.begin(),
                      monitor->entry_queue.end(),
                      thread_id) != monitor->entry_queue.end() ||
            std::any_of(monitor->wait_set.begin(), monitor->wait_set.end(),
                        [thread_id](const std::shared_ptr<WaitNode>& waiter) {
                            return waiter->thread_id == thread_id;
                        });
        if (present) {
            monitor->condition.notify_all();
        }
    }
}

void MonitorTable::wake_all() noexcept {
    std::scoped_lock lock(mutex_);
    for (const auto& [bits, monitor] : monitors_) {
        (void)bits;
        monitor->condition.notify_all();
    }
}

void MonitorTable::release_all(JavaThreadId thread_id) noexcept {
    std::scoped_lock lock(mutex_);
    for (auto iterator = monitors_.begin(); iterator != monitors_.end();) {
        auto& monitor = *iterator->second;
        if (monitor.owner == thread_id) {
            monitor.owner = 0U;
            monitor.recursion = 0U;
        }
        erase_thread(monitor.entry_queue, thread_id);
        for (const auto& waiter : monitor.wait_set) {
            if (waiter->thread_id == thread_id) {
                waiter->notified = true;
            }
        }
        monitor.wait_set.erase(
            std::remove_if(monitor.wait_set.begin(), monitor.wait_set.end(),
                           [thread_id](const std::shared_ptr<WaitNode>& waiter) {
                               return waiter->thread_id == thread_id;
                           }),
            monitor.wait_set.end());
        monitor.condition.notify_all();
        if (monitor.owner == 0U && monitor.entry_queue.empty() &&
            monitor.wait_set.empty()) {
            iterator = monitors_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void MonitorTable::clear() noexcept {
    std::scoped_lock lock(mutex_);
    cancelled_ = true;
    for (const auto& [bits, monitor] : monitors_) {
        (void)bits;
        monitor->cancelled = true;
        monitor->condition.notify_all();
    }
    monitors_.clear();
}

} // namespace phoneme::vm
