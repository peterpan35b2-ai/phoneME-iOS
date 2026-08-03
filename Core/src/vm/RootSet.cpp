#include "phoneme/vm/RootSet.hpp"

#include <algorithm>
#include <limits>

#include "phoneme/base/Checked.hpp"

namespace phoneme::vm {

RootSet::RootSet(usize maximum_roots) noexcept
    : maximum_roots_(std::min(
          maximum_roots,
          static_cast<usize>(std::numeric_limits<u32>::max()) - 1U)) {}

Result<RootHandle> RootSet::pin(ObjectRef reference) {
    std::scoped_lock lock(mutex_);

    usize index = 0U;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        if (slots_.size() >= maximum_roots_) {
            return fail(ErrorCode::overflow, "native root limit reached");
        }
        index = slots_.size();
        slots_.push_back({});
    }

    auto encoded_slot = checked_narrow<u32>(index + 1U);
    if (!encoded_slot) {
        if (index + 1U == slots_.size()) {
            slots_.pop_back();
        } else {
            free_slots_.push_back(index);
        }
        return std::unexpected(encoded_slot.error());
    }

    Slot& slot = slots_[index];
    slot.occupied = true;
    slot.reference = reference;
    ++live_roots_;
    return RootHandle::make(*encoded_slot, slot.generation);
}

Status RootSet::update(RootHandle handle, ObjectRef reference) {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(handle);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    slots_[*slot].reference = reference;
    return {};
}

Status RootSet::unpin(RootHandle handle) noexcept {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(handle);
    if (!slot) {
        return std::unexpected(slot.error());
    }

    Slot& target = slots_[*slot];
    target.occupied = false;
    target.reference = {};
    advance_generation(target);
    free_slots_.push_back(*slot);
    --live_roots_;
    return {};
}

Result<ObjectRef> RootSet::value(RootHandle handle) const {
    std::scoped_lock lock(mutex_);
    auto slot = resolve_slot_unlocked(handle);
    if (!slot) {
        return std::unexpected(slot.error());
    }
    return slots_[*slot].reference;
}

void RootSet::append_reference_roots(std::vector<ObjectRef>& roots) const {
    std::scoped_lock lock(mutex_);
    roots.reserve(roots.size() + live_roots_);
    for (const Slot& slot : slots_) {
        if (slot.occupied && !slot.reference.is_null()) {
            roots.push_back(slot.reference);
        }
    }
}

void RootSet::clear() noexcept {
    std::scoped_lock lock(mutex_);
    free_slots_.clear();
    free_slots_.reserve(slots_.size());
    for (usize index = 0U; index < slots_.size(); ++index) {
        Slot& slot = slots_[index];
        slot.occupied = false;
        slot.reference = {};
        advance_generation(slot);
        free_slots_.push_back(index);
    }
    live_roots_ = 0U;
}

RootSetStats RootSet::stats() const noexcept {
    std::scoped_lock lock(mutex_);
    return RootSetStats {
        .live_roots = live_roots_,
        .slot_count = slots_.size(),
        .maximum_roots = maximum_roots_,
    };
}

Result<usize> RootSet::resolve_slot_unlocked(RootHandle handle) const noexcept {
    if (handle.is_null() || handle.slot() == 0U) {
        return fail(ErrorCode::invalid_argument, "null native root handle");
    }
    const usize index = static_cast<usize>(handle.slot() - 1U);
    if (index >= slots_.size()) {
        return fail(ErrorCode::invalid_argument,
                    "native root handle slot is invalid");
    }
    const Slot& slot = slots_[index];
    if (!slot.occupied || slot.generation != handle.generation()) {
        return fail(ErrorCode::invalid_argument, "stale native root handle");
    }
    return index;
}

void RootSet::advance_generation(Slot& slot) noexcept {
    ++slot.generation;
    if (slot.generation == 0U) {
        slot.generation = 1U;
    }
}

} // namespace phoneme::vm
