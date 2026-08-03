#pragma once

#include <limits>
#include <mutex>
#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

struct RootHandle final {
    u64 bits {0};

    [[nodiscard]] constexpr bool is_null() const noexcept { return bits == 0U; }
    [[nodiscard]] constexpr u32 slot() const noexcept {
        return bits == 0U ? 0U : static_cast<u32>(bits & 0xFFFFFFFFULL);
    }
    [[nodiscard]] constexpr u32 generation() const noexcept {
        return static_cast<u32>(bits >> 32U);
    }

    [[nodiscard]] static constexpr RootHandle make(u32 slot,
                                                   u32 generation) noexcept {
        return RootHandle {(static_cast<u64>(generation) << 32U) |
                           static_cast<u64>(slot)};
    }

    friend constexpr bool operator==(RootHandle, RootHandle) noexcept = default;
};

struct RootSetStats final {
    usize live_roots {0};
    usize slot_count {0};
    usize maximum_roots {0};
};

class RootSet final {
public:
    explicit RootSet(usize maximum_roots = 1'000'000) noexcept;

    [[nodiscard]] Result<RootHandle> pin(ObjectRef reference);
    [[nodiscard]] Status update(RootHandle handle, ObjectRef reference);
    [[nodiscard]] Status unpin(RootHandle handle) noexcept;
    [[nodiscard]] Result<ObjectRef> value(RootHandle handle) const;

    void append_reference_roots(std::vector<ObjectRef>& roots) const;
    void clear() noexcept;
    [[nodiscard]] RootSetStats stats() const noexcept;

private:
    struct Slot final {
        u32 generation {1U};
        bool occupied {false};
        ObjectRef reference {};
    };

    [[nodiscard]] Result<usize> resolve_slot_unlocked(
        RootHandle handle) const noexcept;
    static void advance_generation(Slot& slot) noexcept;

    usize maximum_roots_ {0};
    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    std::vector<usize> free_slots_;
    usize live_roots_ {0};
};

} // namespace phoneme::vm
