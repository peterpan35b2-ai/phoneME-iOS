#include "phoneme/vm/Verifier.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "phoneme/base/Checked.hpp"
#include "phoneme/vm/Descriptor.hpp"

namespace phoneme::vm {
namespace {

constexpr u16 kAccStatic = 0x0008U;
constexpr u16 kAccNative = 0x0100U;
constexpr u16 kAccAbstract = 0x0400U;
constexpr u16 kUnknownReturnAddress = std::numeric_limits<u16>::max();

enum class VerificationValueKind : u8 {
    top,
    category2_tail,
    int32,
    float32,
    int64,
    float64,
    reference,
    uninitialized_this,
    uninitialized_object,
    return_address,
};

struct VerificationValue final {
    VerificationValueKind kind {VerificationValueKind::top};
    u16 origin {0};
    u16 subroutine_target {kUnknownReturnAddress};

    [[nodiscard]] friend bool operator==(const VerificationValue&,
                                         const VerificationValue&) = default;
};

struct FrameState final {
    std::vector<VerificationValue> locals;
    std::vector<VerificationValue> stack;
    usize stack_slots {0};
};

struct TransferResult final {
    FrameState state;
    usize next_pc {0};
    std::vector<usize> successors;
    bool falls_through {true};
};

using JsrReturnSites =
    std::unordered_map<usize, std::vector<usize>>;

class Cursor final {
public:
    Cursor(std::span<const u8> code, usize position)
        : code_(code), position_(position) {}

    [[nodiscard]] usize position() const noexcept { return position_; }

    [[nodiscard]] Result<u8> read_u8(std::string_view context) {
        if (position_ >= code_.size()) {
            return fail(ErrorCode::verification_failed,
                        "truncated bytecode while reading " +
                            std::string(context));
        }
        return code_[position_++];
    }

    [[nodiscard]] Result<u16> read_u16(std::string_view context) {
        auto high = read_u8(context);
        auto low = read_u8(context);
        if (!high) return std::unexpected(high.error());
        if (!low) return std::unexpected(low.error());
        return static_cast<u16>((static_cast<u16>(*high) << 8U) |
                                static_cast<u16>(*low));
    }

    [[nodiscard]] Result<i16> read_i16(std::string_view context) {
        auto value = read_u16(context);
        if (!value) return std::unexpected(value.error());
        return static_cast<i16>(*value);
    }

    [[nodiscard]] Result<i32> read_i32(std::string_view context) {
        auto first = read_u16(context);
        auto second = read_u16(context);
        if (!first) return std::unexpected(first.error());
        if (!second) return std::unexpected(second.error());
        const u32 bits = (static_cast<u32>(*first) << 16U) |
                         static_cast<u32>(*second);
        return static_cast<i32>(bits);
    }

    [[nodiscard]] Status skip(usize count, std::string_view context) {
        if (count > code_.size() - position_) {
            return fail(ErrorCode::verification_failed,
                        "truncated bytecode while skipping " +
                            std::string(context));
        }
        position_ += count;
        return {};
    }

    [[nodiscard]] Status align_switch() {
        // Accept non-zero alignment bytes emitted by several legacy J2ME
        // obfuscators. They are outside the instruction stream; bounds and
        // branch-target validation below remain strict.
        while ((position_ & 3U) != 0U) {
            auto padding = read_u8("switch padding");
            if (!padding) return std::unexpected(padding.error());
        }
        return {};
    }

private:
    std::span<const u8> code_;
    usize position_ {0};
};

[[nodiscard]] std::unexpected<Error> verify_fail(
    const classfile::Method& method,
    usize pc,
    std::string message) {
    return fail(ErrorCode::verification_failed,
                method.name + method.descriptor + " at bytecode " +
                    std::to_string(pc) + ": " + std::move(message));
}

[[nodiscard]] constexpr usize value_width(
    VerificationValue value) noexcept {
    return value.kind == VerificationValueKind::int64 ||
                   value.kind == VerificationValueKind::float64
        ? 2U
        : 1U;
}

[[nodiscard]] constexpr VerifiedSlotKind verified_slot_kind(
    VerificationValue value) noexcept {
    switch (value.kind) {
    case VerificationValueKind::top:
        return VerifiedSlotKind::empty;
    case VerificationValueKind::category2_tail:
        return VerifiedSlotKind::continuation;
    case VerificationValueKind::int32:
        return VerifiedSlotKind::int32;
    case VerificationValueKind::int64:
        return VerifiedSlotKind::int64;
    case VerificationValueKind::float32:
        return VerifiedSlotKind::float32;
    case VerificationValueKind::float64:
        return VerifiedSlotKind::float64;
    case VerificationValueKind::reference:
    case VerificationValueKind::uninitialized_this:
    case VerificationValueKind::uninitialized_object:
        return VerifiedSlotKind::reference;
    case VerificationValueKind::return_address:
        return VerifiedSlotKind::return_address;
    }
    return VerifiedSlotKind::empty;
}

[[nodiscard]] constexpr bool is_reference_like(
    VerificationValue value,
    bool include_uninitialized = false) noexcept {
    if (value.kind == VerificationValueKind::reference) {
        return true;
    }
    return include_uninitialized &&
           (value.kind == VerificationValueKind::uninitialized_this ||
            value.kind == VerificationValueKind::uninitialized_object);
}

[[nodiscard]] VerificationValue value_for_type(
    const TypeDescriptor& descriptor) noexcept {
    switch (descriptor.kind) {
    case JavaTypeKind::boolean:
    case JavaTypeKind::byte:
    case JavaTypeKind::character:
    case JavaTypeKind::short_integer:
    case JavaTypeKind::integer:
        return {.kind = VerificationValueKind::int32};
    case JavaTypeKind::float32:
        return {.kind = VerificationValueKind::float32};
    case JavaTypeKind::long_integer:
        return {.kind = VerificationValueKind::int64};
    case JavaTypeKind::float64:
        return {.kind = VerificationValueKind::float64};
    case JavaTypeKind::reference:
    case JavaTypeKind::array:
        return {.kind = VerificationValueKind::reference};
    case JavaTypeKind::void_type:
        return {.kind = VerificationValueKind::top};
    }
    return {.kind = VerificationValueKind::top};
}

[[nodiscard]] Result<VerificationValue> value_for_stack_map(
    const classfile::VerificationType& type) {
    switch (type.kind) {
    case classfile::VerificationTypeKind::top:
        return VerificationValue {.kind = VerificationValueKind::top};
    case classfile::VerificationTypeKind::integer:
        return VerificationValue {.kind = VerificationValueKind::int32};
    case classfile::VerificationTypeKind::float32:
        return VerificationValue {.kind = VerificationValueKind::float32};
    case classfile::VerificationTypeKind::float64:
        return VerificationValue {.kind = VerificationValueKind::float64};
    case classfile::VerificationTypeKind::long_integer:
        return VerificationValue {.kind = VerificationValueKind::int64};
    case classfile::VerificationTypeKind::null_reference:
    case classfile::VerificationTypeKind::object:
        return VerificationValue {.kind = VerificationValueKind::reference};
    case classfile::VerificationTypeKind::uninitialized_this:
        return VerificationValue {
            .kind = VerificationValueKind::uninitialized_this,
        };
    case classfile::VerificationTypeKind::uninitialized:
        return VerificationValue {
            .kind = VerificationValueKind::uninitialized_object,
            .origin = type.new_instruction_pc,
        };
    }
    return fail(ErrorCode::verification_failed,
                "unknown stack-map verification type");
}

[[nodiscard]] Status push(FrameState& state,
                          VerificationValue value,
                          usize max_stack) {
    if (value.kind == VerificationValueKind::top ||
        value.kind == VerificationValueKind::category2_tail) {
        return fail(ErrorCode::verification_failed,
                    "invalid value pushed to operand stack");
    }
    const usize width = value_width(value);
    auto updated = checked_add(state.stack_slots, width);
    if (!updated || *updated > max_stack) {
        return fail(ErrorCode::verification_failed,
                    "operand stack exceeds max_stack");
    }
    state.stack.push_back(value);
    state.stack_slots = *updated;
    return {};
}

[[nodiscard]] Result<VerificationValue> pop(FrameState& state) {
    if (state.stack.empty()) {
        return fail(ErrorCode::verification_failed,
                    "operand stack underflow");
    }
    const VerificationValue value = state.stack.back();
    state.stack.pop_back();
    state.stack_slots -= value_width(value);
    return value;
}

[[nodiscard]] Result<VerificationValue> pop_kind(
    FrameState& state,
    VerificationValueKind expected) {
    auto value = pop(state);
    if (!value) return std::unexpected(value.error());
    if (value->kind != expected) {
        return fail(ErrorCode::verification_failed,
                    "operand type does not match bytecode instruction");
    }
    return *value;
}

[[nodiscard]] Result<VerificationValue> pop_reference(
    FrameState& state,
    bool allow_uninitialized = false) {
    auto value = pop(state);
    if (!value) return std::unexpected(value.error());
    if (!is_reference_like(*value, allow_uninitialized)) {
        return fail(ErrorCode::verification_failed,
                    "reference operand required");
    }
    return *value;
}

void clear_local_overlap(std::vector<VerificationValue>& locals,
                         usize index) {
    if (index >= locals.size()) return;
    if (locals[index].kind == VerificationValueKind::category2_tail &&
        index > 0U) {
        locals[index - 1U] = {.kind = VerificationValueKind::top};
    }
    if ((locals[index].kind == VerificationValueKind::int64 ||
         locals[index].kind == VerificationValueKind::float64) &&
        index + 1U < locals.size()) {
        locals[index + 1U] = {.kind = VerificationValueKind::top};
    }
    locals[index] = {.kind = VerificationValueKind::top};
}

[[nodiscard]] Status set_local(FrameState& state,
                               usize index,
                               VerificationValue value) {
    const usize width = value_width(value);
    if (index >= state.locals.size() ||
        width > state.locals.size() - index) {
        return fail(ErrorCode::verification_failed,
                    "local variable index exceeds max_locals");
    }
    clear_local_overlap(state.locals, index);
    if (width == 2U) {
        clear_local_overlap(state.locals, index + 1U);
    }
    state.locals[index] = value;
    if (width == 2U) {
        state.locals[index + 1U] = {
            .kind = VerificationValueKind::category2_tail,
        };
    }
    return {};
}

[[nodiscard]] Result<VerificationValue> load_local(
    const FrameState& state,
    usize index,
    VerificationValueKind expected) {
    if (index >= state.locals.size()) {
        return fail(ErrorCode::verification_failed,
                    "local variable index exceeds max_locals");
    }
    const VerificationValue value = state.locals[index];
    if (value.kind != expected) {
        return fail(ErrorCode::verification_failed,
                    "local variable type does not match load opcode");
    }
    if (value_width(value) == 2U &&
        (index + 1U >= state.locals.size() ||
         state.locals[index + 1U].kind !=
             VerificationValueKind::category2_tail)) {
        return fail(ErrorCode::verification_failed,
                    "category-2 local has no second slot");
    }
    return value;
}

[[nodiscard]] Result<VerificationValue> load_reference_local(
    const FrameState& state,
    usize index,
    bool allow_return_address) {
    if (index >= state.locals.size()) {
        return fail(ErrorCode::verification_failed,
                    "local variable index exceeds max_locals");
    }
    const VerificationValue value = state.locals[index];
    if (!is_reference_like(value, true) &&
        !(allow_return_address &&
          value.kind == VerificationValueKind::return_address)) {
        return fail(ErrorCode::verification_failed,
                    "aload/ret local contains an incompatible value");
    }
    return value;
}

[[nodiscard]] Status push_loaded_local(FrameState& state,
                                       usize index,
                                       VerificationValueKind expected,
                                       usize max_stack) {
    auto value = load_local(state, index, expected);
    if (!value) return std::unexpected(value.error());
    return push(state, *value, max_stack);
}

[[nodiscard]] Status push_loaded_reference(FrameState& state,
                                           usize index,
                                           usize max_stack) {
    auto value = load_reference_local(state, index, false);
    if (!value) return std::unexpected(value.error());
    return push(state, *value, max_stack);
}

[[nodiscard]] bool compatible_value(VerificationValue actual,
                                    VerificationValue expected) noexcept {
    if (actual == expected) return true;
    if (actual.kind == VerificationValueKind::reference &&
        expected.kind == VerificationValueKind::reference) {
        return true;
    }
    if (expected.kind == VerificationValueKind::top) {
        return true;
    }
    return false;
}

void normalize_locals(std::vector<VerificationValue>& locals) {
    for (usize index = 0; index < locals.size(); ++index) {
        const auto kind = locals[index].kind;
        if (kind == VerificationValueKind::category2_tail) {
            if (index == 0U ||
                (locals[index - 1U].kind != VerificationValueKind::int64 &&
                 locals[index - 1U].kind != VerificationValueKind::float64)) {
                locals[index] = {.kind = VerificationValueKind::top};
            }
            continue;
        }
        if (kind == VerificationValueKind::int64 ||
            kind == VerificationValueKind::float64) {
            if (index + 1U >= locals.size() ||
                locals[index + 1U].kind !=
                    VerificationValueKind::category2_tail) {
                locals[index] = {.kind = VerificationValueKind::top};
            }
        }
    }
}

[[nodiscard]] Result<bool> merge_state(FrameState& destination,
                                       const FrameState& source) {
    if (destination.locals.size() != source.locals.size() ||
        destination.stack.size() != source.stack.size() ||
        destination.stack_slots != source.stack_slots) {
        return fail(ErrorCode::verification_failed,
                    "control-flow merge has incompatible frame dimensions");
    }

    bool changed = false;
    for (usize index = 0; index < destination.stack.size(); ++index) {
        VerificationValue& current = destination.stack[index];
        const VerificationValue incoming = source.stack[index];
        if (current == incoming) continue;
        if (current.kind == VerificationValueKind::reference &&
            incoming.kind == VerificationValueKind::reference) {
            continue;
        }
        if (current.kind == VerificationValueKind::return_address &&
            incoming.kind == VerificationValueKind::return_address) {
            if (current.origin != incoming.origin &&
                current.origin != kUnknownReturnAddress) {
                current.origin = kUnknownReturnAddress;
                changed = true;
            }
            if (current.subroutine_target != incoming.subroutine_target &&
                current.subroutine_target != kUnknownReturnAddress) {
                current.subroutine_target = kUnknownReturnAddress;
                changed = true;
            }
            continue;
        }
        return fail(ErrorCode::verification_failed,
                    "control-flow merge has incompatible operand types");
    }

    for (usize index = 0; index < destination.locals.size(); ++index) {
        VerificationValue& current = destination.locals[index];
        const VerificationValue incoming = source.locals[index];
        if (current == incoming) continue;
        if (current.kind == VerificationValueKind::reference &&
            incoming.kind == VerificationValueKind::reference) {
            continue;
        }
        if (current.kind == VerificationValueKind::return_address &&
            incoming.kind == VerificationValueKind::return_address) {
            if (current.origin != incoming.origin &&
                current.origin != kUnknownReturnAddress) {
                current.origin = kUnknownReturnAddress;
                changed = true;
            }
            if (current.subroutine_target != incoming.subroutine_target &&
                current.subroutine_target != kUnknownReturnAddress) {
                current.subroutine_target = kUnknownReturnAddress;
                changed = true;
            }
            continue;
        }
        if (current.kind != VerificationValueKind::top) {
            current = {.kind = VerificationValueKind::top};
            changed = true;
        }
    }
    if (changed) normalize_locals(destination.locals);
    return changed;
}

[[nodiscard]] Result<usize> branch_target(usize origin,
                                          i64 offset,
                                          usize code_size) {
    const i64 signed_origin = static_cast<i64>(origin);
    if ((offset > 0 &&
         signed_origin > std::numeric_limits<i64>::max() - offset) ||
        (offset < 0 &&
         signed_origin < std::numeric_limits<i64>::min() - offset)) {
        return fail(ErrorCode::verification_failed,
                    "branch target calculation overflowed");
    }
    const i64 target = signed_origin + offset;
    if (target < 0 ||
        static_cast<u64>(target) >= static_cast<u64>(code_size)) {
        return fail(ErrorCode::verification_failed,
                    "branch target is outside method bytecode");
    }
    return static_cast<usize>(target);
}

[[nodiscard]] Status pop_binary(FrameState& state,
                                VerificationValueKind kind,
                                usize max_stack) {
    auto right = pop_kind(state, kind);
    auto left = pop_kind(state, kind);
    if (!right) return std::unexpected(right.error());
    if (!left) return std::unexpected(left.error());
    return push(state, {.kind = kind}, max_stack);
}

[[nodiscard]] Status pop_shift(FrameState& state,
                               VerificationValueKind value_kind,
                               usize max_stack) {
    auto shift = pop_kind(state, VerificationValueKind::int32);
    auto value = pop_kind(state, value_kind);
    if (!shift) return std::unexpected(shift.error());
    if (!value) return std::unexpected(value.error());
    return push(state, {.kind = value_kind}, max_stack);
}

[[nodiscard]] Status pop_array_access(FrameState& state,
                                      VerificationValueKind result_kind,
                                      usize max_stack) {
    auto index = pop_kind(state, VerificationValueKind::int32);
    auto array = pop_reference(state);
    if (!index) return std::unexpected(index.error());
    if (!array) return std::unexpected(array.error());
    return push(state, {.kind = result_kind}, max_stack);
}

[[nodiscard]] Status pop_array_store(FrameState& state,
                                     VerificationValueKind value_kind) {
    Result<VerificationValue> value = value_kind ==
                                              VerificationValueKind::reference
        ? pop_reference(state)
        : pop_kind(state, value_kind);
    auto index = pop_kind(state, VerificationValueKind::int32);
    auto array = pop_reference(state);
    if (!value) return std::unexpected(value.error());
    if (!index) return std::unexpected(index.error());
    if (!array) return std::unexpected(array.error());
    return {};
}

[[nodiscard]] Status pop_descriptor_value(FrameState& state,
                                          const TypeDescriptor& descriptor) {
    const VerificationValue expected = value_for_type(descriptor);
    if (expected.kind == VerificationValueKind::reference) {
        auto value = pop_reference(state);
        if (!value) return std::unexpected(value.error());
        return {};
    }
    auto value = pop_kind(state, expected.kind);
    if (!value) return std::unexpected(value.error());
    return {};
}

void replace_uninitialized(FrameState& state,
                           VerificationValue initialized) {
    const auto replace = [initialized](VerificationValue& value) {
        if (value == initialized) {
            value = {.kind = VerificationValueKind::reference};
        }
    };
    for (VerificationValue& local : state.locals) replace(local);
    for (VerificationValue& value : state.stack) replace(value);
}

[[nodiscard]] Result<FrameState> initial_state(
    const classfile::Method& method,
    const MethodDescriptor& descriptor,
    usize max_locals) {
    FrameState state;
    state.locals.assign(max_locals,
                        {.kind = VerificationValueKind::top});
    usize local_index = 0;
    if ((method.access_flags & kAccStatic) == 0U) {
        const VerificationValue receiver = method.name == "<init>"
            ? VerificationValue {
                  .kind = VerificationValueKind::uninitialized_this,
              }
            : VerificationValue {
                  .kind = VerificationValueKind::reference,
              };
        auto stored = set_local(state, local_index, receiver);
        if (!stored) return std::unexpected(stored.error());
        ++local_index;
    }
    for (const TypeDescriptor& parameter : descriptor.parameters) {
        const VerificationValue value = value_for_type(parameter);
        auto stored = set_local(state, local_index, value);
        if (!stored) return std::unexpected(stored.error());
        local_index += value_width(value);
    }
    return state;
}

[[nodiscard]] std::vector<VerificationValue> logical_locals(
    const FrameState& state) {
    std::vector<VerificationValue> result;
    for (usize index = 0; index < state.locals.size(); ++index) {
        if (state.locals[index].kind == VerificationValueKind::category2_tail) {
            continue;
        }
        result.push_back(state.locals[index]);
    }
    while (!result.empty() &&
           result.back().kind == VerificationValueKind::top) {
        result.pop_back();
    }
    return result;
}

[[nodiscard]] Result<FrameState> frame_from_logical(
    std::span<const VerificationValue> locals,
    std::span<const VerificationValue> stack,
    usize max_locals,
    usize max_stack) {
    FrameState state;
    state.locals.assign(max_locals,
                        {.kind = VerificationValueKind::top});
    usize local_index = 0;
    for (const VerificationValue value : locals) {
        auto stored = set_local(state, local_index, value);
        if (!stored) return std::unexpected(stored.error());
        local_index += value_width(value);
    }
    for (const VerificationValue value : stack) {
        if (value.kind == VerificationValueKind::top ||
            value.kind == VerificationValueKind::category2_tail) {
            return fail(ErrorCode::verification_failed,
                        "stack map contains unusable operand type");
        }
        auto pushed = push(state, value, max_stack);
        if (!pushed) return std::unexpected(pushed.error());
    }
    return state;
}

[[nodiscard]] Result<std::unordered_map<usize, FrameState>> build_stack_maps(
    const classfile::Method& method,
    const classfile::CodeAttribute& code,
    const FrameState& initial) {
    std::unordered_map<usize, FrameState> result;
    if (code.stack_map_frames.empty()) return result;

    std::vector<VerificationValue> current_locals = logical_locals(initial);
    for (const classfile::StackMapFrame& frame : code.stack_map_frames) {
        std::vector<VerificationValue> stack_values;
        switch (frame.kind) {
        case classfile::StackMapFrameKind::same:
            break;
        case classfile::StackMapFrameKind::same_locals_one_stack:
            for (const auto& type : frame.stack) {
                auto converted = value_for_stack_map(type);
                if (!converted) return std::unexpected(converted.error());
                stack_values.push_back(*converted);
            }
            break;
        case classfile::StackMapFrameKind::chop:
            if (frame.chopped_locals > current_locals.size()) {
                return verify_fail(method,
                                   frame.bytecode_offset,
                                   "chop frame removes unavailable locals");
            }
            current_locals.resize(current_locals.size() -
                                  frame.chopped_locals);
            break;
        case classfile::StackMapFrameKind::append:
            for (const auto& type : frame.locals) {
                auto converted = value_for_stack_map(type);
                if (!converted) return std::unexpected(converted.error());
                current_locals.push_back(*converted);
            }
            break;
        case classfile::StackMapFrameKind::cldc_full:
        case classfile::StackMapFrameKind::full:
            current_locals.clear();
            for (const auto& type : frame.locals) {
                auto converted = value_for_stack_map(type);
                if (!converted) return std::unexpected(converted.error());
                current_locals.push_back(*converted);
            }
            for (const auto& type : frame.stack) {
                auto converted = value_for_stack_map(type);
                if (!converted) return std::unexpected(converted.error());
                stack_values.push_back(*converted);
            }
            break;
        }

        auto state = frame_from_logical(current_locals,
                                        stack_values,
                                        code.max_locals,
                                        code.max_stack);
        if (!state) {
            return verify_fail(method,
                               frame.bytecode_offset,
                               state.error().message);
        }
        const auto [iterator, inserted] = result.emplace(
            static_cast<usize>(frame.bytecode_offset),
            std::move(*state));
        (void)iterator;
        if (!inserted) {
            return verify_fail(method,
                               frame.bytecode_offset,
                               "duplicate stack map frame offset");
        }
    }
    return result;
}

[[nodiscard]] Status validate_against_stack_map(
    const classfile::Method& method,
    usize pc,
    const FrameState& actual,
    const FrameState& expected) {
    if (actual.locals.size() != expected.locals.size() ||
        actual.stack.size() != expected.stack.size() ||
        actual.stack_slots != expected.stack_slots) {
        return verify_fail(method,
                           pc,
                           "incoming state does not match stack map dimensions");
    }
    for (usize index = 0; index < actual.locals.size(); ++index) {
        if (!compatible_value(actual.locals[index], expected.locals[index])) {
            return verify_fail(method,
                               pc,
                               "incoming local type does not match stack map");
        }
    }
    for (usize index = 0; index < actual.stack.size(); ++index) {
        if (!compatible_value(actual.stack[index], expected.stack[index])) {
            return verify_fail(method,
                               pc,
                               "incoming operand type does not match stack map");
        }
    }
    return {};
}

[[nodiscard]] Result<VerificationValue> ldc_type(
    const classfile::ClassFile& owner,
    u16 index,
    bool category_two) {
    auto constant = owner.constant(index);
    if (!constant) return std::unexpected(constant.error());
    switch ((*constant)->kind) {
    case classfile::ConstantKind::integer:
        if (!category_two) return VerificationValue {
            .kind = VerificationValueKind::int32,
        };
        break;
    case classfile::ConstantKind::float32:
        if (!category_two) return VerificationValue {
            .kind = VerificationValueKind::float32,
        };
        break;
    case classfile::ConstantKind::string_ref:
    case classfile::ConstantKind::class_ref:
        if (!category_two) return VerificationValue {
            .kind = VerificationValueKind::reference,
        };
        break;
    case classfile::ConstantKind::long64:
        if (category_two) return VerificationValue {
            .kind = VerificationValueKind::int64,
        };
        break;
    case classfile::ConstantKind::float64:
        if (category_two) return VerificationValue {
            .kind = VerificationValueKind::float64,
        };
        break;
    default:
        break;
    }
    return fail(ErrorCode::verification_failed,
                "ldc constant kind does not match opcode");
}

[[nodiscard]] Result<TransferResult> transfer_instruction(
    const classfile::ClassFile& owner,
    const classfile::Method& method,
    const MethodDescriptor& method_descriptor,
    const classfile::CodeAttribute& code,
    usize pc,
    const FrameState& input,
    const JsrReturnSites& jsr_return_sites) {
    TransferResult result {.state = input};
    Cursor cursor(code.bytecode, pc);
    auto opcode_result = cursor.read_u8("opcode");
    if (!opcode_result) return std::unexpected(opcode_result.error());
    const u8 opcode = *opcode_result;
    const auto push_value = [&result, &code](VerificationValueKind kind)
        -> Status {
        return push(result.state, {.kind = kind}, code.max_stack);
    };
    const auto add_short_branch = [&cursor, &result, pc, &code]() -> Status {
        auto offset = cursor.read_i16("branch offset");
        if (!offset) return std::unexpected(offset.error());
        auto target = branch_target(pc, *offset, code.bytecode.size());
        if (!target) return std::unexpected(target.error());
        result.successors.push_back(*target);
        return {};
    };
    const auto add_ret_successors =
        [&result, &jsr_return_sites](const VerificationValue& address)
            -> Status {
            if (address.origin != kUnknownReturnAddress) {
                result.successors.push_back(address.origin);
                return {};
            }
            if (address.subroutine_target != kUnknownReturnAddress) {
                const auto found = jsr_return_sites.find(
                    address.subroutine_target);
                if (found == jsr_return_sites.end()) {
                    return fail(ErrorCode::verification_failed,
                                "ret subroutine has no jsr return sites");
                }
                result.successors.insert(result.successors.end(),
                                         found->second.begin(),
                                         found->second.end());
                return {};
            }
            for (const auto& [target, sites] : jsr_return_sites) {
                (void)target;
                result.successors.insert(result.successors.end(),
                                         sites.begin(), sites.end());
            }
            return {};
        };
    const bool is_jsr_return_site = std::any_of(
        jsr_return_sites.begin(), jsr_return_sites.end(),
        [pc](const auto& entry) {
            return std::find(entry.second.begin(), entry.second.end(), pc) !=
                   entry.second.end();
        });
    const auto push_reference_local =
        [&result, &code, is_jsr_return_site](usize index) -> Status {
            if (is_jsr_return_site && index < result.state.locals.size() &&
                result.state.locals[index].kind ==
                    VerificationValueKind::top) {
                // Legacy jsr subroutines are shared by multiple caller
                // states. A local untouched by the subroutine can merge to
                // TOP even though each caller has a valid reference at its
                // own return site. The aload opcode supplies that missing
                // caller-specific type information.
                return push(result.state,
                            {.kind = VerificationValueKind::reference},
                            code.max_stack);
            }
            return push_loaded_reference(result.state, index,
                                         code.max_stack);
        };

    switch (opcode) {
    case 0x00:
        break;
    case 0x01:
        if (auto status = push_value(VerificationValueKind::reference); !status)
            return std::unexpected(status.error());
        break;
    case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08:
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0x09: case 0x0A:
        if (auto status = push_value(VerificationValueKind::int64); !status)
            return std::unexpected(status.error());
        break;
    case 0x0B: case 0x0C: case 0x0D:
        if (auto status = push_value(VerificationValueKind::float32); !status)
            return std::unexpected(status.error());
        break;
    case 0x0E: case 0x0F:
        if (auto status = push_value(VerificationValueKind::float64); !status)
            return std::unexpected(status.error());
        break;
    case 0x10:
        if (!cursor.skip(1, "bipush operand"))
            return verify_fail(method, pc, "truncated bipush operand");
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0x11:
        if (!cursor.skip(2, "sipush operand"))
            return verify_fail(method, pc, "truncated sipush operand");
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0x12:
    case 0x13:
    case 0x14: {
        Result<u16> index = opcode == 0x12
            ? [&cursor]() -> Result<u16> {
                  auto value = cursor.read_u8("ldc index");
                  if (!value) return std::unexpected(value.error());
                  return static_cast<u16>(*value);
              }()
            : cursor.read_u16("ldc index");
        if (!index) return std::unexpected(index.error());
        auto type = ldc_type(owner, *index, opcode == 0x14);
        if (!type) return std::unexpected(type.error());
        auto status = push(result.state, *type, code.max_stack);
        if (!status) return std::unexpected(status.error());
        break;
    }
    case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: {
        auto index = cursor.read_u8("load local index");
        if (!index) return std::unexpected(index.error());
        Status status;
        switch (opcode) {
        case 0x15:
            status = push_loaded_local(result.state, *index,
                                       VerificationValueKind::int32,
                                       code.max_stack);
            break;
        case 0x16:
            status = push_loaded_local(result.state, *index,
                                       VerificationValueKind::int64,
                                       code.max_stack);
            break;
        case 0x17:
            status = push_loaded_local(result.state, *index,
                                       VerificationValueKind::float32,
                                       code.max_stack);
            break;
        case 0x18:
            status = push_loaded_local(result.state, *index,
                                       VerificationValueKind::float64,
                                       code.max_stack);
            break;
        default:
            status = push_reference_local(*index);
            break;
        }
        if (!status) return std::unexpected(status.error());
        break;
    }
    case 0x1A: case 0x1B: case 0x1C: case 0x1D:
        if (auto status = push_loaded_local(result.state,
                                            opcode - 0x1AU,
                                            VerificationValueKind::int32,
                                            code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x1E: case 0x1F: case 0x20: case 0x21:
        if (auto status = push_loaded_local(result.state,
                                            opcode - 0x1EU,
                                            VerificationValueKind::int64,
                                            code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x22: case 0x23: case 0x24: case 0x25:
        if (auto status = push_loaded_local(result.state,
                                            opcode - 0x22U,
                                            VerificationValueKind::float32,
                                            code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x26: case 0x27: case 0x28: case 0x29:
        if (auto status = push_loaded_local(result.state,
                                            opcode - 0x26U,
                                            VerificationValueKind::float64,
                                            code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x2A: case 0x2B: case 0x2C: case 0x2D:
        if (auto status = push_reference_local(opcode - 0x2AU);
            !status) return std::unexpected(status.error());
        break;
    case 0x2E: case 0x33: case 0x34: case 0x35:
        if (auto status = pop_array_access(result.state,
                                           VerificationValueKind::int32,
                                           code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x2F:
        if (auto status = pop_array_access(result.state,
                                           VerificationValueKind::int64,
                                           code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x30:
        if (auto status = pop_array_access(result.state,
                                           VerificationValueKind::float32,
                                           code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x31:
        if (auto status = pop_array_access(result.state,
                                           VerificationValueKind::float64,
                                           code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x32:
        if (auto status = pop_array_access(result.state,
                                           VerificationValueKind::reference,
                                           code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: {
        auto index = cursor.read_u8("store local index");
        if (!index) return std::unexpected(index.error());
        Result<VerificationValue> value = opcode == 0x3A
            ? pop(result.state)
            : pop_kind(result.state,
                       opcode == 0x36 ? VerificationValueKind::int32
                       : opcode == 0x37 ? VerificationValueKind::int64
                       : opcode == 0x38 ? VerificationValueKind::float32
                                        : VerificationValueKind::float64);
        if (!value) return std::unexpected(value.error());
        if (opcode == 0x3A &&
            !is_reference_like(*value, true) &&
            value->kind != VerificationValueKind::return_address) {
            return verify_fail(method,
                               pc,
                               "astore requires reference or return address");
        }
        auto stored = set_local(result.state, *index, *value);
        if (!stored) return std::unexpected(stored.error());
        break;
    }
    case 0x3B: case 0x3C: case 0x3D: case 0x3E:
    case 0x3F: case 0x40: case 0x41: case 0x42:
    case 0x43: case 0x44: case 0x45: case 0x46:
    case 0x47: case 0x48: case 0x49: case 0x4A:
    case 0x4B: case 0x4C: case 0x4D: case 0x4E: {
        usize index = 0;
        VerificationValueKind kind = VerificationValueKind::top;
        if (opcode <= 0x3E) {
            index = opcode - 0x3BU;
            kind = VerificationValueKind::int32;
        } else if (opcode <= 0x42) {
            index = opcode - 0x3FU;
            kind = VerificationValueKind::int64;
        } else if (opcode <= 0x46) {
            index = opcode - 0x43U;
            kind = VerificationValueKind::float32;
        } else if (opcode <= 0x4A) {
            index = opcode - 0x47U;
            kind = VerificationValueKind::float64;
        } else {
            index = opcode - 0x4BU;
            auto value = pop(result.state);
            if (!value) return std::unexpected(value.error());
            if (!is_reference_like(*value, true) &&
                value->kind != VerificationValueKind::return_address) {
                return verify_fail(method,
                                   pc,
                                   "astore requires reference or return address");
            }
            auto stored = set_local(result.state, index, *value);
            if (!stored) return std::unexpected(stored.error());
            break;
        }
        auto value = pop_kind(result.state, kind);
        if (!value) return std::unexpected(value.error());
        auto stored = set_local(result.state, index, *value);
        if (!stored) return std::unexpected(stored.error());
        break;
    }
    case 0x4F: case 0x54: case 0x55: case 0x56:
        if (auto status = pop_array_store(result.state,
                                          VerificationValueKind::int32);
            !status) return std::unexpected(status.error());
        break;
    case 0x50:
        if (auto status = pop_array_store(result.state,
                                          VerificationValueKind::int64);
            !status) return std::unexpected(status.error());
        break;
    case 0x51:
        if (auto status = pop_array_store(result.state,
                                          VerificationValueKind::float32);
            !status) return std::unexpected(status.error());
        break;
    case 0x52:
        if (auto status = pop_array_store(result.state,
                                          VerificationValueKind::float64);
            !status) return std::unexpected(status.error());
        break;
    case 0x53:
        if (auto status = pop_array_store(result.state,
                                          VerificationValueKind::reference);
            !status) return std::unexpected(status.error());
        break;
    case 0x57: {
        auto value = pop(result.state);
        if (!value) return std::unexpected(value.error());
        if (value_width(*value) != 1U) {
            return verify_fail(method, pc, "pop cannot remove category-2 value");
        }
        break;
    }
    case 0x58: {
        auto first = pop(result.state);
        if (!first) return std::unexpected(first.error());
        if (value_width(*first) == 1U) {
            auto second = pop(result.state);
            if (!second) return std::unexpected(second.error());
            if (value_width(*second) != 1U) {
                return verify_fail(method,
                                   pc,
                                   "pop2 has invalid category layout");
            }
        }
        break;
    }
    case 0x59: {
        auto value = pop(result.state);
        if (!value) return std::unexpected(value.error());
        if (value_width(*value) != 1U) {
            return verify_fail(method, pc, "dup requires category-1 value");
        }
        if (auto status = push(result.state, *value, code.max_stack); !status)
            return std::unexpected(status.error());
        if (auto status = push(result.state, *value, code.max_stack); !status)
            return std::unexpected(status.error());
        break;
    }
    case 0x5A: {
        auto value1 = pop(result.state);
        auto value2 = pop(result.state);
        if (!value1 || !value2) {
            return verify_fail(method, pc, "dup_x1 stack underflow");
        }
        if (value_width(*value1) != 1U || value_width(*value2) != 1U) {
            return verify_fail(method, pc, "dup_x1 category mismatch");
        }
        for (const auto value : {*value1, *value2, *value1}) {
            auto status = push(result.state, value, code.max_stack);
            if (!status) return std::unexpected(status.error());
        }
        break;
    }
    case 0x5B: {
        auto value1 = pop(result.state);
        auto value2 = pop(result.state);
        if (!value1 || !value2 || value_width(*value1) != 1U) {
            return verify_fail(method, pc, "dup_x2 category mismatch");
        }
        if (value_width(*value2) == 2U) {
            for (const auto value : {*value1, *value2, *value1}) {
                auto status = push(result.state, value, code.max_stack);
                if (!status) return std::unexpected(status.error());
            }
        } else {
            auto value3 = pop(result.state);
            if (!value3 || value_width(*value3) != 1U) {
                return verify_fail(method, pc, "dup_x2 category mismatch");
            }
            for (const auto value : {*value1, *value3, *value2, *value1}) {
                auto status = push(result.state, value, code.max_stack);
                if (!status) return std::unexpected(status.error());
            }
        }
        break;
    }
    case 0x5C: {
        auto value1 = pop(result.state);
        if (!value1) return std::unexpected(value1.error());
        if (value_width(*value1) == 2U) {
            for (const auto value : {*value1, *value1}) {
                auto status = push(result.state, value, code.max_stack);
                if (!status) return std::unexpected(status.error());
            }
        } else {
            auto value2 = pop(result.state);
            if (!value2 || value_width(*value2) != 1U) {
                return verify_fail(method, pc, "dup2 category mismatch");
            }
            for (const auto value : {*value2, *value1, *value2, *value1}) {
                auto status = push(result.state, value, code.max_stack);
                if (!status) return std::unexpected(status.error());
            }
        }
        break;
    }
    case 0x5D: {
        auto value1 = pop(result.state);
        if (!value1) return std::unexpected(value1.error());
        if (value_width(*value1) == 2U) {
            auto value2 = pop(result.state);
            if (!value2 || value_width(*value2) != 1U) {
                return verify_fail(method, pc, "dup2_x1 category mismatch");
            }
            for (const auto value : {*value1, *value2, *value1}) {
                auto status = push(result.state, value, code.max_stack);
                if (!status) return std::unexpected(status.error());
            }
        } else {
            auto value2 = pop(result.state);
            auto value3 = pop(result.state);
            if (!value2 || !value3 || value_width(*value2) != 1U ||
                value_width(*value3) != 1U) {
                return verify_fail(method, pc, "dup2_x1 category mismatch");
            }
            for (const auto value : {*value2, *value1, *value3, *value2, *value1}) {
                auto status = push(result.state, value, code.max_stack);
                if (!status) return std::unexpected(status.error());
            }
        }
        break;
    }
    case 0x5E: {
        auto value1 = pop(result.state);
        if (!value1) return std::unexpected(value1.error());
        if (value_width(*value1) == 2U) {
            auto value2 = pop(result.state);
            if (!value2) return std::unexpected(value2.error());
            if (value_width(*value2) == 2U) {
                for (const auto value : {*value1, *value2, *value1}) {
                    auto status = push(result.state, value, code.max_stack);
                    if (!status) return std::unexpected(status.error());
                }
            } else {
                auto value3 = pop(result.state);
                if (!value3 || value_width(*value3) != 1U) {
                    return verify_fail(method, pc, "dup2_x2 category mismatch");
                }
                for (const auto value : {*value1, *value3, *value2, *value1}) {
                    auto status = push(result.state, value, code.max_stack);
                    if (!status) return std::unexpected(status.error());
                }
            }
        } else {
            auto value2 = pop(result.state);
            auto value3 = pop(result.state);
            if (!value2 || !value3 || value_width(*value2) != 1U) {
                return verify_fail(method, pc, "dup2_x2 category mismatch");
            }
            if (value_width(*value3) == 2U) {
                for (const auto value : {*value2, *value1, *value3, *value2, *value1}) {
                    auto status = push(result.state, value, code.max_stack);
                    if (!status) return std::unexpected(status.error());
                }
            } else {
                auto value4 = pop(result.state);
                if (!value4 || value_width(*value4) != 1U) {
                    return verify_fail(method, pc, "dup2_x2 category mismatch");
                }
                for (const auto value : {*value2, *value1, *value4, *value3,
                                         *value2, *value1}) {
                    auto status = push(result.state, value, code.max_stack);
                    if (!status) return std::unexpected(status.error());
                }
            }
        }
        break;
    }
    case 0x5F: {
        auto value1 = pop(result.state);
        auto value2 = pop(result.state);
        if (!value1 || !value2 || value_width(*value1) != 1U ||
            value_width(*value2) != 1U) {
            return verify_fail(method, pc, "swap requires category-1 values");
        }
        for (const auto value : {*value1, *value2}) {
            auto status = push(result.state, value, code.max_stack);
            if (!status) return std::unexpected(status.error());
        }
        break;
    }
    case 0x60: case 0x64: case 0x68: case 0x6C: case 0x70:
    case 0x7E: case 0x80: case 0x82:
        if (auto status = pop_binary(result.state,
                                     VerificationValueKind::int32,
                                     code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x61: case 0x65: case 0x69: case 0x6D: case 0x71:
    case 0x7F: case 0x81: case 0x83:
        if (auto status = pop_binary(result.state,
                                     VerificationValueKind::int64,
                                     code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x62: case 0x66: case 0x6A: case 0x6E: case 0x72:
        if (auto status = pop_binary(result.state,
                                     VerificationValueKind::float32,
                                     code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x63: case 0x67: case 0x6B: case 0x6F: case 0x73:
        if (auto status = pop_binary(result.state,
                                     VerificationValueKind::float64,
                                     code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x74:
        if (!pop_kind(result.state, VerificationValueKind::int32))
            return verify_fail(method, pc, "ineg requires int");
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0x75:
        if (!pop_kind(result.state, VerificationValueKind::int64))
            return verify_fail(method, pc, "lneg requires long");
        if (auto status = push_value(VerificationValueKind::int64); !status)
            return std::unexpected(status.error());
        break;
    case 0x76:
        if (!pop_kind(result.state, VerificationValueKind::float32))
            return verify_fail(method, pc, "fneg requires float");
        if (auto status = push_value(VerificationValueKind::float32); !status)
            return std::unexpected(status.error());
        break;
    case 0x77:
        if (!pop_kind(result.state, VerificationValueKind::float64))
            return verify_fail(method, pc, "dneg requires double");
        if (auto status = push_value(VerificationValueKind::float64); !status)
            return std::unexpected(status.error());
        break;
    case 0x78: case 0x7A: case 0x7C:
        if (auto status = pop_shift(result.state,
                                    VerificationValueKind::int32,
                                    code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x79: case 0x7B: case 0x7D:
        if (auto status = pop_shift(result.state,
                                    VerificationValueKind::int64,
                                    code.max_stack);
            !status) return std::unexpected(status.error());
        break;
    case 0x84: {
        auto index = cursor.read_u8("iinc local index");
        auto increment = cursor.read_u8("iinc increment");
        if (!index) return std::unexpected(index.error());
        if (!increment) return std::unexpected(increment.error());
        auto local = load_local(result.state,
                                *index,
                                VerificationValueKind::int32);
        if (!local) return std::unexpected(local.error());
        break;
    }
    case 0x85: case 0x86: case 0x87: {
        auto value = pop_kind(result.state, VerificationValueKind::int32);
        if (!value) return std::unexpected(value.error());
        const auto target = opcode == 0x85 ? VerificationValueKind::int64
                          : opcode == 0x86 ? VerificationValueKind::float32
                                           : VerificationValueKind::float64;
        if (auto status = push_value(target); !status)
            return std::unexpected(status.error());
        break;
    }
    case 0x88: case 0x89: case 0x8A: {
        auto value = pop_kind(result.state, VerificationValueKind::int64);
        if (!value) return std::unexpected(value.error());
        const auto target = opcode == 0x88 ? VerificationValueKind::int32
                          : opcode == 0x89 ? VerificationValueKind::float32
                                           : VerificationValueKind::float64;
        if (auto status = push_value(target); !status)
            return std::unexpected(status.error());
        break;
    }
    case 0x8B: case 0x8C: case 0x8D: {
        auto value = pop_kind(result.state, VerificationValueKind::float32);
        if (!value) return std::unexpected(value.error());
        const auto target = opcode == 0x8B ? VerificationValueKind::int32
                          : opcode == 0x8C ? VerificationValueKind::int64
                                           : VerificationValueKind::float64;
        if (auto status = push_value(target); !status)
            return std::unexpected(status.error());
        break;
    }
    case 0x8E: case 0x8F: case 0x90: {
        auto value = pop_kind(result.state, VerificationValueKind::float64);
        if (!value) return std::unexpected(value.error());
        const auto target = opcode == 0x8E ? VerificationValueKind::int32
                          : opcode == 0x8F ? VerificationValueKind::int64
                                           : VerificationValueKind::float32;
        if (auto status = push_value(target); !status)
            return std::unexpected(status.error());
        break;
    }
    case 0x91: case 0x92: case 0x93:
        if (!pop_kind(result.state, VerificationValueKind::int32))
            return verify_fail(method, pc, "integer narrowing requires int");
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0x94:
        if (!pop_kind(result.state, VerificationValueKind::int64) ||
            !pop_kind(result.state, VerificationValueKind::int64)) {
            return verify_fail(method, pc, "lcmp requires two longs");
        }
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0x95: case 0x96:
        if (!pop_kind(result.state, VerificationValueKind::float32) ||
            !pop_kind(result.state, VerificationValueKind::float32)) {
            return verify_fail(method, pc, "float compare requires two floats");
        }
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0x97: case 0x98:
        if (!pop_kind(result.state, VerificationValueKind::float64) ||
            !pop_kind(result.state, VerificationValueKind::float64)) {
            return verify_fail(method, pc, "double compare requires two doubles");
        }
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0x9E:
        if (!pop_kind(result.state, VerificationValueKind::int32))
            return verify_fail(method, pc, "integer branch requires int");
        if (auto status = add_short_branch(); !status)
            return std::unexpected(status.error());
        break;
    case 0x9F: case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4:
        if (!pop_kind(result.state, VerificationValueKind::int32) ||
            !pop_kind(result.state, VerificationValueKind::int32)) {
            return verify_fail(method, pc, "integer comparison requires ints");
        }
        if (auto status = add_short_branch(); !status)
            return std::unexpected(status.error());
        break;
    case 0xA5: case 0xA6:
        if (!pop_reference(result.state) || !pop_reference(result.state)) {
            return verify_fail(method, pc, "reference comparison requires refs");
        }
        if (auto status = add_short_branch(); !status)
            return std::unexpected(status.error());
        break;
    case 0xA7:
        if (auto status = add_short_branch(); !status)
            return std::unexpected(status.error());
        result.falls_through = false;
        break;
    case 0xA8: {
        auto offset = cursor.read_i16("jsr offset");
        if (!offset) return std::unexpected(offset.error());
        const usize return_pc = cursor.position();
        auto target = branch_target(pc, *offset, code.bytecode.size());
        if (!target) return std::unexpected(target.error());
        auto pushed = push(
            result.state,
            {.kind = VerificationValueKind::return_address,
             .origin = static_cast<u16>(return_pc),
             .subroutine_target = static_cast<u16>(*target)},
            code.max_stack);
        if (!pushed) return std::unexpected(pushed.error());
        result.successors.push_back(*target);
        result.falls_through = false;
        break;
    }
    case 0xA9: {
        auto index = cursor.read_u8("ret local index");
        if (!index) return std::unexpected(index.error());
        auto address = load_reference_local(result.state, *index, true);
        if (!address ||
            address->kind != VerificationValueKind::return_address) {
            return verify_fail(method, pc, "ret requires return-address local");
        }
        auto successors = add_ret_successors(*address);
        if (!successors) return std::unexpected(successors.error());
        result.falls_through = false;
        break;
    }
    case 0xAA: {
        auto key = pop_kind(result.state, VerificationValueKind::int32);
        if (!key) return std::unexpected(key.error());
        if (auto status = cursor.align_switch(); !status)
            return std::unexpected(status.error());
        auto default_offset = cursor.read_i32("tableswitch default");
        auto low = cursor.read_i32("tableswitch low");
        auto high = cursor.read_i32("tableswitch high");
        if (!default_offset || !low || !high || *high < *low) {
            return verify_fail(method, pc, "invalid tableswitch header");
        }
        auto default_target = branch_target(pc,
                                            *default_offset,
                                            code.bytecode.size());
        if (!default_target) return std::unexpected(default_target.error());
        result.successors.push_back(*default_target);
        const u64 count = static_cast<u64>(
            static_cast<i64>(*high) - static_cast<i64>(*low) + 1);
        for (u64 index = 0; index < count; ++index) {
            auto offset = cursor.read_i32("tableswitch target");
            if (!offset) return std::unexpected(offset.error());
            auto target = branch_target(pc, *offset, code.bytecode.size());
            if (!target) return std::unexpected(target.error());
            result.successors.push_back(*target);
        }
        result.falls_through = false;
        break;
    }
    case 0xAB: {
        auto key = pop_kind(result.state, VerificationValueKind::int32);
        if (!key) return std::unexpected(key.error());
        if (auto status = cursor.align_switch(); !status)
            return std::unexpected(status.error());
        auto default_offset = cursor.read_i32("lookupswitch default");
        auto count = cursor.read_i32("lookupswitch pair count");
        if (!default_offset || !count || *count < 0) {
            return verify_fail(method, pc, "invalid lookupswitch header");
        }
        auto default_target = branch_target(pc,
                                            *default_offset,
                                            code.bytecode.size());
        if (!default_target) return std::unexpected(default_target.error());
        result.successors.push_back(*default_target);
        for (i32 index = 0; index < *count; ++index) {
            auto match = cursor.read_i32("lookupswitch match");
            auto offset = cursor.read_i32("lookupswitch target");
            if (!match || !offset) {
                return verify_fail(method, pc, "truncated lookupswitch pair");
            }
            auto target = branch_target(pc, *offset, code.bytecode.size());
            if (!target) return std::unexpected(target.error());
            result.successors.push_back(*target);
        }
        result.falls_through = false;
        break;
    }
    case 0xAC: case 0xAD: case 0xAE: case 0xAF: case 0xB0: {
        const VerificationValue expected = value_for_type(
            method_descriptor.return_type);
        const VerificationValueKind opcode_kind =
            opcode == 0xAC ? VerificationValueKind::int32
            : opcode == 0xAD ? VerificationValueKind::int64
            : opcode == 0xAE ? VerificationValueKind::float32
            : opcode == 0xAF ? VerificationValueKind::float64
                             : VerificationValueKind::reference;
        if (expected.kind != opcode_kind) {
            return verify_fail(method, pc, "return opcode mismatches descriptor");
        }
        Result<VerificationValue> returned = opcode == 0xB0
            ? pop_reference(result.state)
            : pop_kind(result.state, opcode_kind);
        if (!returned) return std::unexpected(returned.error());
        // Returning discards the entire current frame. Values below the
        // returned operand are legal and are emitted by several real-world
        // obfuscators, including Java 8 J2ME builds.
        result.falls_through = false;
        break;
    }
    case 0xB1:
        if (method_descriptor.return_type.kind != JavaTypeKind::void_type) {
            return verify_fail(method, pc, "void return mismatches descriptor");
        }
        if (method.name == "<init>" &&
            std::any_of(result.state.locals.begin(),
                        result.state.locals.end(),
                        [](VerificationValue value) {
                            return value.kind ==
                                VerificationValueKind::uninitialized_this;
                        })) {
            return verify_fail(method,
                               pc,
                               "constructor returns before this is initialized");
        }
        // A void return also discards all remaining operands with the frame.
        result.falls_through = false;
        break;
    case 0xB2: case 0xB3: case 0xB4: case 0xB5: {
        auto index = cursor.read_u16("field reference index");
        if (!index) return std::unexpected(index.error());
        auto reference = owner.member_reference(*index);
        if (!reference ||
            reference->kind != classfile::ConstantKind::field_ref) {
            return verify_fail(method, pc, "invalid field reference");
        }
        auto descriptor = parse_field_descriptor(reference->descriptor);
        if (!descriptor) return std::unexpected(descriptor.error());
        if (opcode == 0xB3 || opcode == 0xB5) {
            auto popped = pop_descriptor_value(result.state, *descriptor);
            if (!popped) return std::unexpected(popped.error());
        }
        if (opcode == 0xB4 || opcode == 0xB5) {
            // A constructor may initialize fields declared by its own class
            // before invoking the superclass constructor. javac uses this for
            // synthetic outer-instance fields in non-static inner classes.
            // This JVMS rule is not limited to legacy class-file versions.
            const bool allow_uninitialized_this =
                opcode == 0xB5 && method.name == "<init>" &&
                reference->owner == owner.name();
            auto receiver = pop_reference(
                result.state, allow_uninitialized_this);
            if (!receiver) return std::unexpected(receiver.error());
            if (receiver->kind != VerificationValueKind::reference &&
                !(allow_uninitialized_this &&
                  receiver->kind ==
                      VerificationValueKind::uninitialized_this)) {
                return verify_fail(method, pc,
                                   "field access requires initialized receiver");
            }
        }
        if (opcode == 0xB2 || opcode == 0xB4) {
            auto status = push(result.state,
                               value_for_type(*descriptor),
                               code.max_stack);
            if (!status) return std::unexpected(status.error());
        }
        break;
    }
    case 0xB6: case 0xB7: case 0xB8: case 0xB9: {
        auto index = cursor.read_u16("method reference index");
        if (!index) return std::unexpected(index.error());
        if (opcode == 0xB9) {
            auto count = cursor.read_u8("invokeinterface count");
            auto zero = cursor.read_u8("invokeinterface zero");
            if (!count || !zero || *zero != 0U) {
                return verify_fail(method, pc, "invalid invokeinterface operands");
            }
        }
        auto reference = owner.member_reference(*index);
        if (!reference) return std::unexpected(reference.error());
        auto descriptor = parse_method_descriptor(reference->descriptor);
        if (!descriptor) return std::unexpected(descriptor.error());
        for (usize reverse = descriptor->parameters.size(); reverse > 0; --reverse) {
            auto popped = pop_descriptor_value(
                result.state,
                descriptor->parameters[reverse - 1U]);
            if (!popped) return std::unexpected(popped.error());
        }
        if (opcode != 0xB8) {
            auto receiver = pop_reference(result.state,
                                          opcode == 0xB7 &&
                                              reference->name == "<init>");
            if (!receiver) return std::unexpected(receiver.error());
            if (opcode == 0xB7 && reference->name == "<init>") {
                if (receiver->kind != VerificationValueKind::uninitialized_this &&
                    receiver->kind !=
                        VerificationValueKind::uninitialized_object) {
                    return verify_fail(method,
                                       pc,
                                       "constructor call requires uninitialized receiver");
                }
                replace_uninitialized(result.state, *receiver);
            } else if (receiver->kind != VerificationValueKind::reference) {
                return verify_fail(method,
                                   pc,
                                   "method call requires initialized receiver");
            }
        }
        if (descriptor->return_type.kind != JavaTypeKind::void_type) {
            auto status = push(result.state,
                               value_for_type(descriptor->return_type),
                               code.max_stack);
            if (!status) return std::unexpected(status.error());
        }
        break;
    }
    case 0xBA: {
        auto index = cursor.read_u16("invokedynamic index");
        auto zero1 = cursor.read_u8("invokedynamic zero");
        auto zero2 = cursor.read_u8("invokedynamic zero");
        if (!index || !zero1 || !zero2 || *zero1 != 0U || *zero2 != 0U) {
            return verify_fail(method, pc, "invalid invokedynamic operands");
        }
        auto reference = owner.invoke_dynamic_reference(*index);
        if (!reference) {
            return std::unexpected(reference.error());
        }
        auto descriptor = parse_method_descriptor(reference->descriptor);
        if (!descriptor) {
            return std::unexpected(descriptor.error());
        }
        for (usize reverse = descriptor->parameters.size(); reverse > 0;
             --reverse) {
            auto popped = pop_descriptor_value(
                result.state,
                descriptor->parameters[reverse - 1U]);
            if (!popped) {
                return std::unexpected(popped.error());
            }
        }
        if (descriptor->return_type.kind != JavaTypeKind::void_type) {
            auto status = push(result.state,
                               value_for_type(descriptor->return_type),
                               code.max_stack);
            if (!status) {
                return std::unexpected(status.error());
            }
        }
        break;
    }
    case 0xBB: {
        auto index = cursor.read_u16("new class index");
        if (!index) return std::unexpected(index.error());
        auto class_name = owner.class_name_constant(*index);
        if (!class_name) return std::unexpected(class_name.error());
        auto status = push(result.state,
                           {.kind = VerificationValueKind::uninitialized_object,
                            .origin = static_cast<u16>(pc)},
                           code.max_stack);
        if (!status) return std::unexpected(status.error());
        break;
    }
    case 0xBC:
        if (!cursor.skip(1, "newarray type"))
            return verify_fail(method, pc, "truncated newarray type");
        if (!pop_kind(result.state, VerificationValueKind::int32))
            return verify_fail(method, pc, "newarray length must be int");
        if (auto status = push_value(VerificationValueKind::reference); !status)
            return std::unexpected(status.error());
        break;
    case 0xBD:
        if (!cursor.skip(2, "anewarray class index"))
            return verify_fail(method, pc, "truncated anewarray index");
        if (!pop_kind(result.state, VerificationValueKind::int32))
            return verify_fail(method, pc, "anewarray length must be int");
        if (auto status = push_value(VerificationValueKind::reference); !status)
            return std::unexpected(status.error());
        break;
    case 0xBE:
        if (!pop_reference(result.state))
            return verify_fail(method, pc, "arraylength requires array reference");
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0xBF:
        if (!pop_reference(result.state))
            return verify_fail(method, pc, "athrow requires initialized reference");
        result.falls_through = false;
        break;
    case 0xC0:
        if (!cursor.skip(2, "checkcast class index"))
            return verify_fail(method, pc, "truncated checkcast index");
        if (!pop_reference(result.state))
            return verify_fail(method, pc, "checkcast requires reference");
        if (auto status = push_value(VerificationValueKind::reference); !status)
            return std::unexpected(status.error());
        break;
    case 0xC1:
        if (!cursor.skip(2, "instanceof class index"))
            return verify_fail(method, pc, "truncated instanceof index");
        if (!pop_reference(result.state))
            return verify_fail(method, pc, "instanceof requires reference");
        if (auto status = push_value(VerificationValueKind::int32); !status)
            return std::unexpected(status.error());
        break;
    case 0xC2: case 0xC3:
        if (!pop_reference(result.state))
            return verify_fail(method, pc, "monitor operation requires reference");
        break;
    case 0xC4: {
        auto widened = cursor.read_u8("wide opcode");
        auto index = cursor.read_u16("wide local index");
        if (!widened || !index) {
            return verify_fail(method, pc, "truncated wide instruction");
        }
        Status status;
        switch (*widened) {
        case 0x15:
            status = push_loaded_local(result.state, *index,
                                       VerificationValueKind::int32,
                                       code.max_stack);
            break;
        case 0x16:
            status = push_loaded_local(result.state, *index,
                                       VerificationValueKind::int64,
                                       code.max_stack);
            break;
        case 0x17:
            status = push_loaded_local(result.state, *index,
                                       VerificationValueKind::float32,
                                       code.max_stack);
            break;
        case 0x18:
            status = push_loaded_local(result.state, *index,
                                       VerificationValueKind::float64,
                                       code.max_stack);
            break;
        case 0x19:
            status = push_reference_local(*index);
            break;
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A: {
            Result<VerificationValue> value = *widened == 0x3A
                ? pop(result.state)
                : pop_kind(result.state,
                           *widened == 0x36 ? VerificationValueKind::int32
                           : *widened == 0x37 ? VerificationValueKind::int64
                           : *widened == 0x38 ? VerificationValueKind::float32
                                              : VerificationValueKind::float64);
            if (!value) return std::unexpected(value.error());
            if (*widened == 0x3A &&
                !is_reference_like(*value, true) &&
                value->kind != VerificationValueKind::return_address) {
                return verify_fail(method, pc, "wide astore type mismatch");
            }
            status = set_local(result.state, *index, *value);
            break;
        }
        case 0x84: {
            auto increment = cursor.read_u16("wide iinc increment");
            if (!increment) return std::unexpected(increment.error());
            auto local = load_local(result.state,
                                    *index,
                                    VerificationValueKind::int32);
            if (!local) return std::unexpected(local.error());
            status = {};
            break;
        }
        case 0xA9: {
            auto address = load_reference_local(result.state, *index, true);
            if (!address ||
                address->kind != VerificationValueKind::return_address) {
                return verify_fail(method, pc, "wide ret type mismatch");
            }
            auto successors = add_ret_successors(*address);
            if (!successors) return std::unexpected(successors.error());
            result.falls_through = false;
            status = {};
            break;
        }
        default:
            return verify_fail(method, pc, "wide modifies invalid opcode");
        }
        if (!status) return std::unexpected(status.error());
        break;
    }
    case 0xC5: {
        auto index = cursor.read_u16("multianewarray class index");
        auto dimensions = cursor.read_u8("multianewarray dimensions");
        if (!index || !dimensions || *dimensions == 0U) {
            return verify_fail(method, pc, "invalid multianewarray operands");
        }
        for (u8 dimension = 0; dimension < *dimensions; ++dimension) {
            auto length = pop_kind(result.state,
                                   VerificationValueKind::int32);
            if (!length) return std::unexpected(length.error());
        }
        if (auto status = push_value(VerificationValueKind::reference); !status)
            return std::unexpected(status.error());
        break;
    }
    case 0xC6: case 0xC7:
        if (!pop_reference(result.state))
            return verify_fail(method, pc, "null branch requires reference");
        if (auto status = add_short_branch(); !status)
            return std::unexpected(status.error());
        break;
    case 0xC8: {
        auto offset = cursor.read_i32("goto_w offset");
        if (!offset) return std::unexpected(offset.error());
        auto target = branch_target(pc, *offset, code.bytecode.size());
        if (!target) return std::unexpected(target.error());
        result.successors.push_back(*target);
        result.falls_through = false;
        break;
    }
    case 0xC9: {
        auto offset = cursor.read_i32("jsr_w offset");
        if (!offset) return std::unexpected(offset.error());
        const usize return_pc = cursor.position();
        auto target = branch_target(pc, *offset, code.bytecode.size());
        if (!target) return std::unexpected(target.error());
        auto pushed = push(
            result.state,
            {.kind = VerificationValueKind::return_address,
             .origin = static_cast<u16>(return_pc),
             .subroutine_target = static_cast<u16>(*target)},
            code.max_stack);
        if (!pushed) return std::unexpected(pushed.error());
        result.successors.push_back(*target);
        result.falls_through = false;
        break;
    }
    default:
        return verify_fail(method, pc, "opcode is not supported by verifier");
    }

    result.next_pc = cursor.position();
    if (result.falls_through) {
        if (result.next_pc >= code.bytecode.size()) {
            return verify_fail(method,
                               pc,
                               "execution falls off the end of the method");
        }
        result.successors.push_back(result.next_pc);
    }
    std::sort(result.successors.begin(), result.successors.end());
    result.successors.erase(std::unique(result.successors.begin(),
                                        result.successors.end()),
                            result.successors.end());
    return result;
}

[[nodiscard]] Result<JsrReturnSites> collect_jsr_return_sites(
    std::span<const u8> bytecode) {
    JsrReturnSites sites;
    Cursor cursor(bytecode, 0);
    while (cursor.position() < bytecode.size()) {
        const usize pc = cursor.position();
        auto opcode = cursor.read_u8("opcode");
        if (!opcode) return std::unexpected(opcode.error());
        if (*opcode == 0xA8U) {
            auto offset = cursor.read_i16("jsr offset");
            if (!offset) return std::unexpected(offset.error());
            auto target = branch_target(pc, *offset, bytecode.size());
            if (!target) return std::unexpected(target.error());
            sites[*target].push_back(cursor.position());
            continue;
        }
        if (*opcode == 0xC9U) {
            auto offset = cursor.read_i32("jsr_w offset");
            if (!offset) return std::unexpected(offset.error());
            auto target = branch_target(pc, *offset, bytecode.size());
            if (!target) return std::unexpected(target.error());
            sites[*target].push_back(cursor.position());
            continue;
        }

        // Structural verification has already decoded every instruction. This
        // compact scanner only needs exact lengths to collect jsr return sites.
        if (*opcode <= 0x0FU ||
            (*opcode >= 0x1AU && *opcode <= 0x35U) ||
            (*opcode >= 0x3BU && *opcode <= 0x83U) ||
            (*opcode >= 0x85U && *opcode <= 0x98U) ||
            (*opcode >= 0xACU && *opcode <= 0xB1U) ||
            *opcode == 0xBEU || *opcode == 0xBFU ||
            *opcode == 0xC2U || *opcode == 0xC3U) {
            continue;
        }
        if (*opcode == 0x10U || *opcode == 0x12U ||
            (*opcode >= 0x15U && *opcode <= 0x19U) ||
            (*opcode >= 0x36U && *opcode <= 0x3AU) ||
            *opcode == 0xA9U || *opcode == 0xBCU) {
            auto skipped = cursor.skip(1, "single-byte operand");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if (*opcode == 0x11U || *opcode == 0x13U || *opcode == 0x14U ||
            *opcode == 0x84U ||
            (*opcode >= 0x99U && *opcode <= 0xA7U) ||
            (*opcode >= 0xB2U && *opcode <= 0xB8U) ||
            *opcode == 0xBBU || *opcode == 0xBDU ||
            *opcode == 0xC0U || *opcode == 0xC1U ||
            *opcode == 0xC6U || *opcode == 0xC7U) {
            auto skipped = cursor.skip(2, "two-byte operand");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if (*opcode == 0xAAU) {
            if (auto status = cursor.align_switch(); !status)
                return std::unexpected(status.error());
            auto default_offset = cursor.read_i32("tableswitch default");
            auto low = cursor.read_i32("tableswitch low");
            auto high = cursor.read_i32("tableswitch high");
            if (!default_offset || !low || !high || *high < *low) {
                return fail(ErrorCode::verification_failed,
                            "invalid tableswitch while scanning jsr sites");
            }
            const usize count = static_cast<usize>(
                static_cast<i64>(*high) - static_cast<i64>(*low) + 1);
            auto skipped = cursor.skip(count * 4U, "tableswitch targets");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if (*opcode == 0xABU) {
            if (auto status = cursor.align_switch(); !status)
                return std::unexpected(status.error());
            auto default_offset = cursor.read_i32("lookupswitch default");
            auto count = cursor.read_i32("lookupswitch pair count");
            if (!default_offset || !count || *count < 0) {
                return fail(ErrorCode::verification_failed,
                            "invalid lookupswitch while scanning jsr sites");
            }
            auto skipped = cursor.skip(static_cast<usize>(*count) * 8U,
                                       "lookupswitch pairs");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if (*opcode == 0xB9U || *opcode == 0xBAU) {
            auto skipped = cursor.skip(4, "four-byte invoke operands");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if (*opcode == 0xC4U) {
            auto widened = cursor.read_u8("wide opcode");
            if (!widened) return std::unexpected(widened.error());
            auto skipped = cursor.skip(*widened == 0x84U ? 4U : 2U,
                                       "wide operands");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if (*opcode == 0xC5U) {
            auto skipped = cursor.skip(3, "multianewarray operands");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if (*opcode == 0xC8U) {
            auto skipped = cursor.skip(4, "goto_w offset");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        return fail(ErrorCode::verification_failed,
                    "cannot scan bytecode instruction at " +
                        std::to_string(pc));
    }
    for (auto& [target, return_sites] : sites) {
        (void)target;
        std::sort(return_sites.begin(), return_sites.end());
        return_sites.erase(
            std::unique(return_sites.begin(), return_sites.end()),
            return_sites.end());
    }
    return sites;
}

void build_verified_reference_maps(
    const classfile::CodeAttribute& code,
    const std::unordered_map<usize, FrameState>& states,
    VerifiedMethodReferenceMaps& output) {
    output.max_locals = code.max_locals;
    output.max_stack = code.max_stack;
    output.frames.clear();
    output.frames.reserve(states.size());

    for (const auto& [bytecode_pc, state] : states) {
        VerifiedReferenceMap frame {
            .bytecode_pc = bytecode_pc,
            .stack_slots = state.stack_slots,
        };
        frame.reference_slots.reserve(
            state.locals.size() + state.stack.size());
        frame.slot_kinds.reserve(code.max_locals + state.stack_slots);
        for (usize index = 0U; index < state.locals.size(); ++index) {
            const VerificationValue value = state.locals[index];
            frame.slot_kinds.push_back(verified_slot_kind(value));
            if (is_reference_like(value, true)) {
                frame.reference_slots.push_back(index);
            }
        }
        usize stack_slot = code.max_locals;
        for (const VerificationValue value : state.stack) {
            frame.slot_kinds.push_back(verified_slot_kind(value));
            if (is_reference_like(value, true)) {
                frame.reference_slots.push_back(stack_slot);
            }
            if (value_width(value) == 2U) {
                frame.slot_kinds.push_back(VerifiedSlotKind::continuation);
            }
            stack_slot += value_width(value);
        }
        output.frames.push_back(std::move(frame));
    }
    std::sort(output.frames.begin(),
              output.frames.end(),
              [](const VerifiedReferenceMap& left,
                 const VerifiedReferenceMap& right) {
                  return left.bytecode_pc < right.bytecode_pc;
              });
}

} // namespace

[[nodiscard]] Status verify_method_impl(
    const classfile::ClassFile& owner,
    const classfile::Method& method,
    bool enforce_stack_maps,
    VerifiedMethodReferenceMaps* reference_maps = nullptr) {
    auto descriptor = parse_method_descriptor(method.descriptor);
    if (!descriptor) {
        return fail(ErrorCode::verification_failed,
                    method.name + method.descriptor +
                        ": invalid method descriptor: " +
                        descriptor.error().message);
    }

    const bool is_abstract = (method.access_flags & kAccAbstract) != 0U;
    const bool is_native = (method.access_flags & kAccNative) != 0U;
    if (is_abstract || is_native) {
        if (method.code.has_value()) {
            return fail(ErrorCode::verification_failed,
                        method.name + method.descriptor +
                            ": abstract/native method contains Code");
        }
        if (reference_maps != nullptr) {
            *reference_maps = VerifiedMethodReferenceMaps {};
        }
        return {};
    }
    if (!method.code.has_value()) {
        return fail(ErrorCode::verification_failed,
                    method.name + method.descriptor +
                        ": concrete method has no Code attribute");
    }
    if (method.name == "<clinit>" &&
        (((method.access_flags & kAccStatic) == 0U) ||
         method.descriptor != "()V")) {
        return fail(ErrorCode::verification_failed,
                    "<clinit> must be static and use descriptor ()V");
    }
    if (method.name == "<init>" &&
        (((method.access_flags & kAccStatic) != 0U) ||
         descriptor->return_type.kind != JavaTypeKind::void_type)) {
        return fail(ErrorCode::verification_failed,
                    "<init> must be non-static and return void");
    }

    const classfile::CodeAttribute& code = *method.code;
    const usize required_locals = descriptor->parameter_slots(
        (method.access_flags & kAccStatic) == 0U);
    if (required_locals > code.max_locals) {
        return fail(ErrorCode::verification_failed,
                    method.name + method.descriptor +
                        ": parameters exceed max_locals");
    }

    auto initial = initial_state(method,
                                 *descriptor,
                                 code.max_locals);
    if (!initial) {
        return fail(ErrorCode::verification_failed,
                    method.name + method.descriptor + ": " +
                        initial.error().message);
    }
    Result<std::unordered_map<usize, FrameState>> stack_maps =
        enforce_stack_maps
            ? build_stack_maps(method, code, *initial)
            : Result<std::unordered_map<usize, FrameState>>(
                  std::unordered_map<usize, FrameState> {});
    if (!stack_maps) return std::unexpected(stack_maps.error());
    auto jsr_return_sites = collect_jsr_return_sites(code.bytecode);
    if (!jsr_return_sites) return std::unexpected(jsr_return_sites.error());

    FrameState entry_state = *initial;
    if (const auto entry_map = stack_maps->find(0U);
        entry_map != stack_maps->end()) {
        auto valid = validate_against_stack_map(method,
                                                0U,
                                                entry_state,
                                                entry_map->second);
        if (!valid) return valid;
        entry_state = entry_map->second;
    }

    std::unordered_map<usize, FrameState> states;
    states.emplace(0U, std::move(entry_state));
    std::deque<usize> worklist {0U};
    std::unordered_set<usize> queued {0U};

    const auto enqueue = [&](usize target,
                             const FrameState& incoming,
                             auto&& enqueue_ref) -> Status {
        if (target >= code.bytecode.size()) {
            return verify_fail(method, target, "successor is outside bytecode");
        }
        FrameState candidate = incoming;
        if (const auto map = stack_maps->find(target);
            map != stack_maps->end()) {
            auto valid = validate_against_stack_map(method,
                                                    target,
                                                    candidate,
                                                    map->second);
            if (!valid) return valid;
            candidate = map->second;
        }

        const auto iterator = states.find(target);
        if (iterator == states.end()) {
            states.emplace(target, std::move(candidate));
            if (queued.insert(target).second) worklist.push_back(target);
            return {};
        }
        auto merged = merge_state(iterator->second, candidate);
        if (!merged) return std::unexpected(merged.error());
        if (*merged && queued.insert(target).second) {
            worklist.push_back(target);
        }
        (void)enqueue_ref;
        return {};
    };

    while (!worklist.empty()) {
        const usize pc = worklist.front();
        worklist.pop_front();
        queued.erase(pc);
        const FrameState input = states.at(pc);

        auto transfer = transfer_instruction(owner,
                                             method,
                                             *descriptor,
                                             code,
                                             pc,
                                             input,
                                             *jsr_return_sites);
        if (!transfer) {
            const std::string prefix = method.name + method.descriptor +
                " at bytecode ";
            if (transfer.error().code == ErrorCode::verification_failed &&
                transfer.error().message.starts_with(prefix)) {
                return std::unexpected(transfer.error());
            }
            return verify_fail(method, pc, transfer.error().message);
        }

        for (const classfile::ExceptionHandler& handler :
             code.exception_table) {
            if (pc < static_cast<usize>(handler.start_pc) ||
                pc >= static_cast<usize>(handler.end_pc)) {
                continue;
            }
            FrameState handler_state = input;
            handler_state.stack.clear();
            handler_state.stack_slots = 0;
            auto pushed = push(handler_state,
                               {.kind = VerificationValueKind::reference},
                               code.max_stack);
            if (!pushed) {
                return verify_fail(method, pc, pushed.error().message);
            }
            auto status = enqueue(handler.handler_pc,
                                  handler_state,
                                  enqueue);
            if (!status) return status;
        }
        for (const usize successor : transfer->successors) {
            auto status = enqueue(successor,
                                  transfer->state,
                                  enqueue);
            if (!status) return status;
        }
    }

    for (const auto& [offset, expected] : *stack_maps) {
        (void)expected;
        if (!states.contains(offset)) {
            // Unreachable StackMap entries are legal after dead code removal.
            continue;
        }
    }
    if (reference_maps != nullptr) {
        build_verified_reference_maps(code, states, *reference_maps);
    }
    return {};
}

Result<VerifiedMethodReferenceMaps> verified_reference_maps(
    const classfile::ClassFile& owner,
    const classfile::Method& method) {
    VerifiedMethodReferenceMaps maps;
    auto strict = verify_method_impl(owner, method, true, &maps);
    if (strict) return maps;
    if (strict.error().code != ErrorCode::verification_failed ||
        owner.major_version() > 50U || !method.code.has_value()) {
        return std::unexpected(strict.error());
    }

    const bool has_cldc_stack_map = std::any_of(
        method.code->stack_map_frames.begin(),
        method.code->stack_map_frames.end(),
        [](const classfile::StackMapFrame& frame) {
            return frame.kind == classfile::StackMapFrameKind::cldc_full;
        });
    if (!has_cldc_stack_map) return std::unexpected(strict.error());

    maps = VerifiedMethodReferenceMaps {};
    auto relaxed = verify_method_impl(owner, method, false, &maps);
    if (!relaxed) return std::unexpected(relaxed.error());
    return maps;
}

Status verify_method(const classfile::ClassFile& owner,
                     const classfile::Method& method) {
    auto strict = verify_method_impl(owner, method, true);
    if (strict || strict.error().code != ErrorCode::verification_failed ||
        owner.major_version() > 50U || !method.code.has_value()) {
        return strict;
    }

    const bool has_cldc_stack_map = std::any_of(
        method.code->stack_map_frames.begin(),
        method.code->stack_map_frames.end(),
        [](const classfile::StackMapFrame& frame) {
            return frame.kind == classfile::StackMapFrameKind::cldc_full;
        });
    if (!has_cldc_stack_map) return strict;

    // Some preverified MIDP 1.x/2.x JARs contain stale CLDC StackMap metadata
    // after obfuscation. The fallback still performs complete bytecode
    // data-flow verification; it only ignores the legacy hint table.
    return verify_method_impl(owner, method, false);
}

Status verify_class(const classfile::ClassFile& class_file) {
    std::unordered_set<std::string> field_keys;
    for (const classfile::Field& field : class_file.fields()) {
        auto descriptor = parse_field_descriptor(field.descriptor);
        if (!descriptor || descriptor->kind == JavaTypeKind::void_type) {
            return fail(ErrorCode::verification_failed,
                        class_file.name() + ": invalid field descriptor for " +
                            field.name);
        }
        const std::string key = field.name + "\n" + field.descriptor;
        if (!field_keys.insert(key).second) {
            return fail(ErrorCode::verification_failed,
                        class_file.name() + ": duplicate field " + field.name);
        }
    }

    std::unordered_set<std::string> method_keys;
    for (const classfile::Method& method : class_file.methods()) {
        const std::string key = method.name + "\n" + method.descriptor;
        if (!method_keys.insert(key).second) {
            return fail(ErrorCode::verification_failed,
                        class_file.name() + ": duplicate method " + method.name);
        }
        auto verified = verify_method(class_file, method);
        if (!verified) return verified;
    }
    return {};
}

} // namespace phoneme::vm
