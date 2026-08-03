#include "phoneme/vm/NativeRootScope.hpp"

namespace phoneme::vm {

NativeRootScope::~NativeRootScope() {
    abandon();
}

NativeRootScope::NativeRootScope(NativeRootScope&& other) noexcept
    : roots_(std::exchange(other.roots_, nullptr)),
      handle_(std::exchange(other.handle_, {})) {}

NativeRootScope& NativeRootScope::operator=(NativeRootScope&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    abandon();
    roots_ = std::exchange(other.roots_, nullptr);
    handle_ = std::exchange(other.handle_, {});
    return *this;
}

Result<NativeRootScope> NativeRootScope::pin(RootSet& roots,
                                             ObjectRef reference) {
    auto handle = roots.pin(reference);
    if (!handle) {
        return std::unexpected(handle.error());
    }
    return NativeRootScope(roots, *handle);
}

Status NativeRootScope::reset(ObjectRef reference) {
    if (!active()) {
        return fail(ErrorCode::invalid_state,
                    "native root scope is not active");
    }
    return roots_->update(handle_, reference);
}

Result<ObjectRef> NativeRootScope::get() const {
    if (!active()) {
        return fail(ErrorCode::invalid_state,
                    "native root scope is not active");
    }
    return roots_->value(handle_);
}

Status NativeRootScope::release() noexcept {
    if (!active()) {
        return {};
    }
    RootSet* roots = roots_;
    const RootHandle handle = handle_;
    roots_ = nullptr;
    handle_ = {};
    return roots->unpin(handle);
}

void NativeRootScope::abandon() noexcept {
    if (!active()) {
        return;
    }
    (void)roots_->unpin(handle_);
    roots_ = nullptr;
    handle_ = {};
}

} // namespace phoneme::vm
