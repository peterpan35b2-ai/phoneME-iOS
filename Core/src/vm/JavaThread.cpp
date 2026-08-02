#include "phoneme/vm/JavaThread.hpp"

namespace phoneme::vm {

JavaThread::JavaThread(JavaThreadId thread_id,
                       ObjectRef thread_object,
                       ObjectRef runnable_target)
    : id_(thread_id),
      object_(thread_object),
      target_(runnable_target),
      context_(std::make_shared<ExecutionContext>(thread_id)) {}

JavaThreadSnapshot JavaThread::snapshot() const noexcept {
    std::scoped_lock lock(mutex_);
    return JavaThreadSnapshot {
        .id = id_,
        .object = object_,
        .target = target_,
        .state = state_,
        .priority = priority_,
        .interrupted = interrupted_,
        .alive = started_ && state_ != JavaThreadState::terminated,
    };
}

} // namespace phoneme::vm
