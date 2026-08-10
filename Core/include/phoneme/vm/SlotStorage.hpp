#pragma once

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

// Most J2ME methods are tiny wrappers/getters whose max_stack/max_locals are
// well below 16 slots. Keeping those slots inside the execution frame avoids
// several malloc/free pairs for every interpreted Java call. Large/obfuscated
// methods transparently fall back to the original vector-backed storage.
inline constexpr usize kInlineFrameSlotCapacity = 16U;

class OperandStack final {
public:
    explicit OperandStack(usize maximum_slots)
        : maximum_slots_(maximum_slots),
          inline_mode_(maximum_slots <= kInlineFrameSlotCapacity) {
        if (!inline_mode_) {
            values_.reserve(maximum_slots);
        }
    }

    OperandStack(const OperandStack&) = delete;
    OperandStack& operator=(const OperandStack&) = delete;

    OperandStack(OperandStack&& other) noexcept
        : maximum_slots_(other.maximum_slots_),
          used_slots_(other.used_slots_),
          value_count_(other.value_count_),
          inline_mode_(other.inline_mode_) {
        if (inline_mode_) {
            std::copy_n(other.inline_values_.begin(),
                        value_count_,
                        inline_values_.begin());
        } else {
            values_ = std::move(other.values_);
        }
    }

    OperandStack& operator=(OperandStack&& other) noexcept {
        if (this == &other) return *this;
        maximum_slots_ = other.maximum_slots_;
        used_slots_ = other.used_slots_;
        value_count_ = other.value_count_;
        inline_mode_ = other.inline_mode_;
        if (inline_mode_) {
            values_.clear();
            std::copy_n(other.inline_values_.begin(),
                        value_count_,
                        inline_values_.begin());
        } else {
            values_ = std::move(other.values_);
        }
        return *this;
    }

    [[nodiscard]] usize used_slots() const noexcept { return used_slots_; }
    [[nodiscard]] bool empty() const noexcept { return value_count() == 0U; }

    [[nodiscard]] Status push(Value value) {
        if (value.empty() || value.continuation_slot()) {
            return fail(ErrorCode::invalid_argument,
                        "cannot push an empty or continuation value");
        }
        const usize required = value.category_two() ? 2U : 1U;
        if (required > maximum_slots_ - used_slots_) {
            return fail(ErrorCode::malformed_class,
                        "operand stack exceeds max_stack slots");
        }
        if (inline_mode_) {
            inline_values_[value_count_++] = value;
        } else {
            values_.push_back(value);
        }
        used_slots_ += required;
        return {};
    }

    [[nodiscard]] Result<Value> pop() {
        if (empty()) {
            return fail(ErrorCode::malformed_class,
                        "operand stack underflow");
        }
        Value value;
        if (inline_mode_) {
            value = inline_values_[--value_count_];
        } else {
            value = values_.back();
            values_.pop_back();
        }
        used_slots_ -= value.category_two() ? 2U : 1U;
        return value;
    }

    [[nodiscard]] Result<const Value*> top() const {
        if (empty()) {
            return fail(ErrorCode::malformed_class,
                        "operand stack is empty");
        }
        return inline_mode_
            ? &inline_values_[value_count_ - 1U]
            : &values_.back();
    }

    void clear() noexcept {
        if (inline_mode_) {
            value_count_ = 0U;
        } else {
            values_.clear();
        }
        used_slots_ = 0U;
    }

    void append_reference_roots(std::vector<ObjectRef>& roots) const {
        const usize count = value_count();
        for (usize index = 0U; index < count; ++index) {
            const Value value = value_at(index);
            if (value.kind() != ValueKind::reference) continue;
            const ObjectRef reference = value.reference_unchecked();
            if (!reference.is_null()) roots.push_back(reference);
        }
    }

    void append_jit_physical_bits(std::vector<u64>& output) const {
        output.reserve(output.size() + used_slots_);
        const usize count = value_count();
        for (usize index = 0U; index < count; ++index) {
            const Value value = value_at(index);
            output.push_back(value.raw_bits_unchecked());
            if (value.category_two()) output.push_back(0U);
        }
    }

private:
    [[nodiscard]] usize value_count() const noexcept {
        return inline_mode_ ? value_count_ : values_.size();
    }

    [[nodiscard]] Value value_at(usize index) const noexcept {
        return inline_mode_ ? inline_values_[index] : values_[index];
    }

    usize maximum_slots_ {0U};
    usize used_slots_ {0U};
    usize value_count_ {0U};
    bool inline_mode_ {true};
    std::array<Value, kInlineFrameSlotCapacity> inline_values_ {};
    std::vector<Value> values_;
};

class LocalVariables final {
public:
    explicit LocalVariables(usize slot_count)
        : slot_count_(slot_count),
          inline_mode_(slot_count <= kInlineFrameSlotCapacity) {
        if (!inline_mode_) {
            slots_.resize(slot_count);
            reference_positions_.resize(slot_count);
            reference_indices_.reserve(slot_count);
        }
    }

    LocalVariables(const LocalVariables&) = delete;
    LocalVariables& operator=(const LocalVariables&) = delete;

    LocalVariables(LocalVariables&& other) noexcept
        : slot_count_(other.slot_count_),
          inline_reference_count_(other.inline_reference_count_),
          inline_mode_(other.inline_mode_) {
        if (inline_mode_) {
            std::copy_n(other.inline_slots_.begin(),
                        slot_count_,
                        inline_slots_.begin());
            std::copy_n(other.inline_reference_positions_.begin(),
                        slot_count_,
                        inline_reference_positions_.begin());
            std::copy_n(other.inline_reference_indices_.begin(),
                        inline_reference_count_,
                        inline_reference_indices_.begin());
        } else {
            slots_ = std::move(other.slots_);
            reference_indices_ = std::move(other.reference_indices_);
            reference_positions_ = std::move(other.reference_positions_);
        }
    }

    LocalVariables& operator=(LocalVariables&& other) noexcept {
        if (this == &other) return *this;
        slot_count_ = other.slot_count_;
        inline_reference_count_ = other.inline_reference_count_;
        inline_mode_ = other.inline_mode_;
        if (inline_mode_) {
            slots_.clear();
            reference_indices_.clear();
            reference_positions_.clear();
            std::copy_n(other.inline_slots_.begin(),
                        slot_count_,
                        inline_slots_.begin());
            std::copy_n(other.inline_reference_positions_.begin(),
                        slot_count_,
                        inline_reference_positions_.begin());
            std::copy_n(other.inline_reference_indices_.begin(),
                        inline_reference_count_,
                        inline_reference_indices_.begin());
        } else {
            slots_ = std::move(other.slots_);
            reference_indices_ = std::move(other.reference_indices_);
            reference_positions_ = std::move(other.reference_positions_);
        }
        return *this;
    }

    [[nodiscard]] usize slot_count() const noexcept { return slot_count_; }

    [[nodiscard]] Status set(usize index, Value value) {
        if (value.empty() || value.continuation_slot()) {
            return fail(ErrorCode::invalid_argument,
                        "cannot store an empty or continuation value");
        }
        const usize required = value.category_two() ? 2U : 1U;
        if (index >= slot_count_ || required > slot_count_ - index) {
            return fail(ErrorCode::malformed_class,
                        "local-variable write exceeds max_locals slots");
        }

        clear_value_covering(index);
        if (required == 2U) {
            clear_value_covering(index + 1U);
        }
        slot(index) = value;
        if (value.kind() == ValueKind::reference) {
            mark_reference(index);
        }
        if (required == 2U) {
            slot(index + 1U) = Value::continuation();
        }
        return {};
    }

    void clear() noexcept {
        if (inline_mode_) {
            std::fill_n(inline_slots_.begin(), slot_count_, Value {});
            std::fill_n(inline_reference_positions_.begin(), slot_count_, 0U);
            inline_reference_count_ = 0U;
        } else {
            std::fill(slots_.begin(), slots_.end(), Value {});
            std::fill(reference_positions_.begin(),
                      reference_positions_.end(),
                      0U);
            reference_indices_.clear();
        }
    }

    [[nodiscard]] Result<Value> get(usize index) const {
        if (index >= slot_count_) {
            return fail(ErrorCode::malformed_class,
                        "local-variable index exceeds max_locals slots");
        }
        const Value value = slot(index);
        if (value.empty()) {
            return fail(ErrorCode::malformed_class,
                        "read from an uninitialized local variable");
        }
        if (value.continuation_slot()) {
            return fail(ErrorCode::malformed_class,
                        "read from the second slot of a category-2 local");
        }
        if (value.category_two() &&
            (index + 1U >= slot_count_ ||
             !slot(index + 1U).continuation_slot())) {
            return fail(ErrorCode::malformed_class,
                        "corrupt category-2 local variable layout");
        }
        return value;
    }

    void append_reference_roots(std::vector<ObjectRef>& roots) const {
        const usize count = reference_count();
        for (usize position = 0U; position < count; ++position) {
            const usize index = reference_index(position);
            const ObjectRef reference = slot(index).reference_unchecked();
            if (!reference.is_null()) roots.push_back(reference);
        }
    }

    void append_jit_physical_bits(std::vector<u64>& output) const {
        output.reserve(output.size() + slot_count_);
        for (usize index = 0U; index < slot_count_; ++index) {
            const Value value = slot(index);
            output.push_back(value.empty() || value.continuation_slot()
                                 ? 0U
                                 : value.raw_bits_unchecked());
        }
    }

private:
    [[nodiscard]] Value& slot(usize index) noexcept {
        return inline_mode_ ? inline_slots_[index] : slots_[index];
    }

    [[nodiscard]] const Value& slot(usize index) const noexcept {
        return inline_mode_ ? inline_slots_[index] : slots_[index];
    }

    [[nodiscard]] usize reference_count() const noexcept {
        return inline_mode_ ? inline_reference_count_ : reference_indices_.size();
    }

    [[nodiscard]] usize reference_index(usize position) const noexcept {
        return inline_mode_
            ? static_cast<usize>(inline_reference_indices_[position])
            : reference_indices_[position];
    }

    [[nodiscard]] usize reference_position(usize index) const noexcept {
        return inline_mode_
            ? static_cast<usize>(inline_reference_positions_[index])
            : reference_positions_[index];
    }

    void set_reference_position(usize index, usize position) noexcept {
        if (inline_mode_) {
            inline_reference_positions_[index] = static_cast<u8>(position);
        } else {
            reference_positions_[index] = position;
        }
    }

    void push_reference_index(usize index) {
        if (inline_mode_) {
            inline_reference_indices_[inline_reference_count_++] =
                static_cast<u8>(index);
        } else {
            reference_indices_.push_back(index);
        }
    }

    void pop_reference_index() noexcept {
        if (inline_mode_) {
            --inline_reference_count_;
        } else {
            reference_indices_.pop_back();
        }
    }

    void set_reference_index(usize position, usize index) noexcept {
        if (inline_mode_) {
            inline_reference_indices_[position] = static_cast<u8>(index);
        } else {
            reference_indices_[position] = index;
        }
    }

    void mark_reference(usize index) {
        if (reference_position(index) != 0U) return;
        push_reference_index(index);
        set_reference_position(index, reference_count());
    }

    void unmark_reference(usize index) noexcept {
        const usize position = reference_position(index);
        if (position == 0U) return;
        const usize vector_index = position - 1U;
        const usize replacement = reference_index(reference_count() - 1U);
        set_reference_index(vector_index, replacement);
        set_reference_position(replacement, position);
        pop_reference_index();
        set_reference_position(index, 0U);
    }

    void clear_value_covering(usize index) noexcept {
        if (index >= slot_count_) {
            return;
        }
        if (slot(index).continuation_slot()) {
            if (index > 0U) {
                unmark_reference(index - 1U);
                slot(index - 1U) = {};
            }
            slot(index) = {};
            return;
        }
        unmark_reference(index);
        if (slot(index).category_two() && index + 1U < slot_count_) {
            slot(index + 1U) = {};
        }
        slot(index) = {};
    }

    usize slot_count_ {0U};
    usize inline_reference_count_ {0U};
    bool inline_mode_ {true};
    std::array<Value, kInlineFrameSlotCapacity> inline_slots_ {};
    std::array<u8, kInlineFrameSlotCapacity> inline_reference_indices_ {};
    std::array<u8, kInlineFrameSlotCapacity> inline_reference_positions_ {};
    std::vector<Value> slots_;
    std::vector<usize> reference_indices_;
    std::vector<usize> reference_positions_;
};

} // namespace phoneme::vm
