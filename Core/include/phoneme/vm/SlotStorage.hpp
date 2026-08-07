#pragma once

#include <algorithm>
#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class OperandStack final {
public:
    explicit OperandStack(usize maximum_slots) : maximum_slots_(maximum_slots) {
        values_.reserve(maximum_slots);
    }

    [[nodiscard]] usize used_slots() const noexcept { return used_slots_; }
    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

    [[nodiscard]] Status push(Value value) {
        if (value.empty() || value.continuation_slot()) {
            return fail(ErrorCode::invalid_argument,
                        "cannot push an empty or continuation value");
        }
        const usize required = value.category_two() ? 2 : 1;
        if (required > maximum_slots_ - used_slots_) {
            return fail(ErrorCode::malformed_class,
                        "operand stack exceeds max_stack slots");
        }
        values_.push_back(value);
        used_slots_ += required;
        return {};
    }

    [[nodiscard]] Result<Value> pop() {
        if (values_.empty()) {
            return fail(ErrorCode::malformed_class,
                        "operand stack underflow");
        }
        Value value = values_.back();
        values_.pop_back();
        used_slots_ -= value.category_two() ? 2 : 1;
        return value;
    }

    [[nodiscard]] Result<const Value*> top() const {
        if (values_.empty()) {
            return fail(ErrorCode::malformed_class,
                        "operand stack is empty");
        }
        return &values_.back();
    }

    void clear() noexcept {
        values_.clear();
        used_slots_ = 0;
    }

    void append_reference_roots(std::vector<ObjectRef>& roots) const {
        for (const Value value : values_) {
            if (value.kind() != ValueKind::reference) continue;
            const ObjectRef reference = value.reference_unchecked();
            if (!reference.is_null()) roots.push_back(reference);
        }
    }

    void append_jit_physical_bits(std::vector<u64>& output) const {
        output.reserve(output.size() + used_slots_);
        for (const Value value : values_) {
            output.push_back(value.raw_bits_unchecked());
            if (value.category_two()) output.push_back(0U);
        }
    }

private:
    usize maximum_slots_ {0};
    usize used_slots_ {0};
    std::vector<Value> values_;
};

class LocalVariables final {
public:
    explicit LocalVariables(usize slot_count)
        : slots_(slot_count), reference_positions_(slot_count) {
        reference_indices_.reserve(slot_count);
    }

    [[nodiscard]] usize slot_count() const noexcept { return slots_.size(); }

    [[nodiscard]] Status set(usize index, Value value) {
        if (value.empty() || value.continuation_slot()) {
            return fail(ErrorCode::invalid_argument,
                        "cannot store an empty or continuation value");
        }
        const usize required = value.category_two() ? 2 : 1;
        if (index >= slots_.size() || required > slots_.size() - index) {
            return fail(ErrorCode::malformed_class,
                        "local-variable write exceeds max_locals slots");
        }

        clear_value_covering(index);
        if (required == 2) {
            clear_value_covering(index + 1);
        }
        slots_[index] = value;
        if (value.kind() == ValueKind::reference) {
            mark_reference(index);
        }
        if (required == 2) {
            slots_[index + 1] = Value::continuation();
        }
        return {};
    }

    void clear() noexcept {
        std::fill(slots_.begin(), slots_.end(), Value {});
        std::fill(reference_positions_.begin(), reference_positions_.end(), 0U);
        reference_indices_.clear();
    }

    [[nodiscard]] Result<Value> get(usize index) const {
        if (index >= slots_.size()) {
            return fail(ErrorCode::malformed_class,
                        "local-variable index exceeds max_locals slots");
        }
        const Value value = slots_[index];
        if (value.empty()) {
            return fail(ErrorCode::malformed_class,
                        "read from an uninitialized local variable");
        }
        if (value.continuation_slot()) {
            return fail(ErrorCode::malformed_class,
                        "read from the second slot of a category-2 local");
        }
        if (value.category_two() &&
            (index + 1 >= slots_.size() ||
             !slots_[index + 1].continuation_slot())) {
            return fail(ErrorCode::malformed_class,
                        "corrupt category-2 local variable layout");
        }
        return value;
    }

    void append_reference_roots(std::vector<ObjectRef>& roots) const {
        for (const usize index : reference_indices_) {
            const ObjectRef reference = slots_[index].reference_unchecked();
            if (!reference.is_null()) roots.push_back(reference);
        }
    }

    void append_jit_physical_bits(std::vector<u64>& output) const {
        output.reserve(output.size() + slots_.size());
        for (const Value value : slots_) {
            output.push_back(value.empty() || value.continuation_slot()
                                 ? 0U
                                 : value.raw_bits_unchecked());
        }
    }

private:
    void mark_reference(usize index) {
        if (reference_positions_[index] != 0U) return;
        reference_indices_.push_back(index);
        reference_positions_[index] = reference_indices_.size();
    }

    void unmark_reference(usize index) noexcept {
        const usize position = reference_positions_[index];
        if (position == 0U) return;
        const usize vector_index = position - 1U;
        const usize replacement = reference_indices_.back();
        reference_indices_[vector_index] = replacement;
        reference_positions_[replacement] = position;
        reference_indices_.pop_back();
        reference_positions_[index] = 0U;
    }

    void clear_value_covering(usize index) noexcept {
        if (index >= slots_.size()) {
            return;
        }
        if (slots_[index].continuation_slot()) {
            if (index > 0) {
                unmark_reference(index - 1U);
                slots_[index - 1U] = {};
            }
            slots_[index] = {};
            return;
        }
        unmark_reference(index);
        if (slots_[index].category_two() && index + 1 < slots_.size()) {
            slots_[index + 1U] = {};
        }
        slots_[index] = {};
    }

    std::vector<Value> slots_;
    std::vector<usize> reference_indices_;
    std::vector<usize> reference_positions_;
};

} // namespace phoneme::vm
