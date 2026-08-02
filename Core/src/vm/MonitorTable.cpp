#include "phoneme/vm/MonitorTable.hpp"

#include <limits>

namespace phoneme::vm {

Result<MonitorEnterResult> MonitorTable::enter(ObjectRef object,
                                               JavaThreadId thread_id) {
    if (object.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot enter monitor for null reference");
    }
    if (thread_id == 0U) {
        return fail(ErrorCode::invalid_argument,
                    "Java thread id zero is reserved");
    }

    std::scoped_lock lock(mutex_);
    Monitor& monitor = monitors_[object.bits];
    if (monitor.owner == 0U) {
        monitor.owner = thread_id;
        monitor.recursion = 1U;
        return MonitorEnterResult::acquired;
    }
    if (monitor.owner == thread_id) {
        if (monitor.recursion == std::numeric_limits<u32>::max()) {
            return fail(ErrorCode::overflow,
                        "monitor recursion count overflowed");
        }
        ++monitor.recursion;
        return MonitorEnterResult::acquired;
    }
    if (monitor.waiters != std::numeric_limits<u32>::max()) {
        ++monitor.waiters;
    }
    return MonitorEnterResult::would_block;
}

Status MonitorTable::exit(ObjectRef object, JavaThreadId thread_id) {
    if (object.is_null()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot exit monitor for null reference");
    }
    if (thread_id == 0U) {
        return fail(ErrorCode::invalid_argument,
                    "Java thread id zero is reserved");
    }

    std::scoped_lock lock(mutex_);
    const auto iterator = monitors_.find(object.bits);
    if (iterator == monitors_.end() || iterator->second.owner != thread_id ||
        iterator->second.recursion == 0U) {
        return fail(ErrorCode::invalid_state,
                    "current Java thread does not own monitor");
    }

    Monitor& monitor = iterator->second;
    --monitor.recursion;
    if (monitor.recursion == 0U) {
        monitor.owner = 0U;
        if (monitor.waiters > 0U) {
            --monitor.waiters;
        }
        if (monitor.waiters == 0U) {
            monitors_.erase(iterator);
        }
    }
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
        .owner = iterator->second.owner,
        .recursion = iterator->second.recursion,
        .waiters = iterator->second.waiters,
    };
}

void MonitorTable::append_reference_roots(
    std::vector<ObjectRef>& roots) const {
    std::scoped_lock lock(mutex_);
    roots.reserve(roots.size() + monitors_.size());
    for (const auto& [bits, monitor] : monitors_) {
        (void)monitor;
        roots.push_back(ObjectRef {bits});
    }
}

void MonitorTable::release_all(JavaThreadId thread_id) noexcept {
    if (thread_id == 0U) return;
    std::scoped_lock lock(mutex_);
    for (auto iterator = monitors_.begin(); iterator != monitors_.end();) {
        Monitor& monitor = iterator->second;
        if (monitor.owner == thread_id) {
            monitor.owner = 0U;
            monitor.recursion = 0U;
            if (monitor.waiters > 0U) {
                --monitor.waiters;
            }
        }
        if (monitor.owner == 0U && monitor.waiters == 0U) {
            iterator = monitors_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void MonitorTable::clear() noexcept {
    std::scoped_lock lock(mutex_);
    monitors_.clear();
}

} // namespace phoneme::vm
