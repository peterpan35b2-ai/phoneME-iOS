#pragma once

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
            auto reference = value.as_reference();
            if (reference && !reference->is_null()) {
                roots.push_back(*reference);
            }
        }
    }

private:
    usize maximum_slots_ {0};
    usize used_slots_ {0};
    std::vector<Value> values_;
};

class LocalVariables final {
public:
    explicit LocalVariables(usize slot_count) : slots_(slot_count) {}

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
        if (required == 2) {
            slots_[index + 1] = Value::continuation();
        }
        return {};
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
        for (const Value value : slots_) {
            if (value.kind() != ValueKind::reference) continue;
            auto reference = value.as_reference();
            if (reference && !reference->is_null()) {
                roots.push_back(*reference);
            }
        }
    }

private:
    void clear_value_covering(usize index) noexcept {
        if (index >= slots_.size()) {
            return;
        }
        if (slots_[index].continuation_slot()) {
            if (index > 0) {
                slots_[index - 1] = {};
            }
            slots_[index] = {};
            return;
        }
        if (slots_[index].category_two() && index + 1 < slots_.size()) {
            slots_[index + 1] = {};
        }
        slots_[index] = {};
    }

    std::vector<Value> slots_;
};

} // namespace phoneme::vm
