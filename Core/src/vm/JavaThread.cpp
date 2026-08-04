#include "phoneme/vm/JavaThread.hpp"

namespace phoneme::vm {

JavaThread::JavaThread(JavaThreadId thread_id,
                       ObjectRef thread_object,
                       ObjectRef runnable_target)
    : id_(thread_id),
      object_(thread_object),
      target_(runnable_target),
      context_(std::make_shared<ExecutionContext>(thread_id)) {}

JavaThread::~JavaThread() {
    // A very short native task can publish its terminated state before its
    // worker lambda has released the final shared_ptr capture. If another host
    // thread retires the scheduler record at exactly that point, the final
    // JavaThread reference may be released on the worker itself. std::jthread's
    // default destructor would then try to join the current thread and abort
    // with EDEADLK. The worker has finished all JavaThread access by destructor
    // entry, so detaching this already-exiting self handle is the only safe
    // ownership transition; all non-self destruction keeps jthread's normal
    // stop-and-join behavior.
    if (worker_.joinable() &&
        worker_.get_id() == std::this_thread::get_id()) {
        worker_.detach();
    }
}

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
