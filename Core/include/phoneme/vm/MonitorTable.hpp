#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

using JavaThreadId = u32;

enum class MonitorEnterResult : u8 {
    acquired,
    would_block,
};

enum class MonitorWaitResult : u8 {
    notified,
    timed_out,
    interrupted,
};

struct MonitorSnapshot final {
    JavaThreadId owner {0};
    u32 recursion {0};
    u32 waiters {0};
};

class MonitorTable final {
public:
    using BlockHook = std::function<void()>;
    using InterruptCheck = std::function<bool()>;

    [[nodiscard]] Result<MonitorEnterResult> enter(ObjectRef object,
                                                   JavaThreadId thread_id);
    [[nodiscard]] Status enter_blocking(ObjectRef object,
                                        JavaThreadId thread_id,
                                        BlockHook before_block,
                                        BlockHook after_block);
    [[nodiscard]] Status exit(ObjectRef object, JavaThreadId thread_id);

    [[nodiscard]] Result<MonitorWaitResult> wait(
        ObjectRef object,
        JavaThreadId thread_id,
        std::optional<std::chrono::milliseconds> timeout,
        BlockHook before_block,
        BlockHook after_block,
        InterruptCheck interrupted);
    [[nodiscard]] Status notify_one(ObjectRef object,
                                    JavaThreadId thread_id);
    [[nodiscard]] Status notify_all(ObjectRef object,
                                    JavaThreadId thread_id);

    [[nodiscard]] Result<MonitorSnapshot> snapshot(ObjectRef object) const;
    void append_reference_roots(std::vector<ObjectRef>& roots) const;
    void wake_thread(JavaThreadId thread_id) noexcept;
    void wake_all() noexcept;
    void release_all(JavaThreadId thread_id) noexcept;
    void clear() noexcept;

private:
    struct WaitNode final {
        JavaThreadId thread_id {0};
        bool notified {false};
    };

    struct Monitor final {
        JavaThreadId owner {0};
        u32 recursion {0};
        std::deque<JavaThreadId> entry_queue;
        std::deque<std::shared_ptr<WaitNode>> wait_set;
        std::condition_variable condition;
        bool cancelled {false};
    };

    [[nodiscard]] std::shared_ptr<Monitor> monitor_locked(ObjectRef object);
    static void enqueue_unique(std::deque<JavaThreadId>& queue,
                               JavaThreadId thread_id);
    static void erase_thread(std::deque<JavaThreadId>& queue,
                             JavaThreadId thread_id) noexcept;
    static u32 waiter_count(const Monitor& monitor) noexcept;
    void erase_if_unused_locked(u64 object_bits,
                                const std::shared_ptr<Monitor>& monitor);

    mutable std::mutex mutex_;
    std::unordered_map<u64, std::shared_ptr<Monitor>> monitors_;
    bool cancelled_ {false};
};

} // namespace phoneme::vm
