#pragma once

#include <utility>

#include "phoneme/vm/RootSet.hpp"

namespace phoneme::vm {

class NativeRootScope final {
public:
    NativeRootScope() noexcept = default;
    ~NativeRootScope();

    NativeRootScope(const NativeRootScope&) = delete;
    NativeRootScope& operator=(const NativeRootScope&) = delete;

    NativeRootScope(NativeRootScope&& other) noexcept;
    NativeRootScope& operator=(NativeRootScope&& other) noexcept;

    [[nodiscard]] static Result<NativeRootScope> pin(
        RootSet& roots,
        ObjectRef reference = {});

    [[nodiscard]] Status reset(ObjectRef reference);
    [[nodiscard]] Result<ObjectRef> get() const;
    [[nodiscard]] Status release() noexcept;
    [[nodiscard]] RootHandle handle() const noexcept { return handle_; }
    [[nodiscard]] bool active() const noexcept {
        return roots_ != nullptr && !handle_.is_null();
    }

private:
    NativeRootScope(RootSet& roots, RootHandle handle) noexcept
        : roots_(&roots), handle_(handle) {}

    void abandon() noexcept;

    RootSet* roots_ {nullptr};
    RootHandle handle_ {};
};

} // namespace phoneme::vm
