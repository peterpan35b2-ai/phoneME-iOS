#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

#include "phoneme/base/Error.hpp"
#include "phoneme/vm/ExecutionContext.hpp"

namespace phoneme::vm {

enum class JavaThreadState : u8 {
    new_thread,
    runnable,
    running,
    blocked_monitor,
    blocked_io,
    waiting,
    sleeping,
    joining,
    terminated,
};

struct JavaThreadSnapshot final {
    JavaThreadId id {0};
    ObjectRef object {};
    ObjectRef target {};
    JavaThreadState state {JavaThreadState::new_thread};
    i32 priority {5};
    bool interrupted {false};
    bool alive {false};
};

class JavaThread final {
public:
    JavaThread(JavaThreadId thread_id,
               ObjectRef thread_object,
               ObjectRef runnable_target);
    ~JavaThread() = default;

    JavaThread(const JavaThread&) = delete;
    JavaThread& operator=(const JavaThread&) = delete;

    [[nodiscard]] JavaThreadSnapshot snapshot() const noexcept;

private:
    friend class Scheduler;

    JavaThreadId id_ {0};
    ObjectRef object_ {};
    ObjectRef target_ {};
    std::shared_ptr<ExecutionContext> context_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::jthread worker_;
    JavaThreadState state_ {JavaThreadState::new_thread};
    i32 priority_ {5};
    bool interrupted_ {false};
    bool started_ {false};
    bool stop_requested_ {false};
    std::optional<ObjectRef> uncaught_throwable_;
    std::optional<Error> native_failure_;
};

} // namespace phoneme::vm
