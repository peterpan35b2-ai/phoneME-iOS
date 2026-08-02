#include "phoneme/vm/Interpreter.hpp"

#include <limits>

#include "phoneme/vm/SlotStorage.hpp"

namespace phoneme::vm {
namespace {

class Frame final {
public:
    Frame(const classfile::CodeAttribute& code,
          std::span<const Value> initial_locals)
        : code_(code.bytecode),
          locals_(code.max_locals),
          stack_(code.max_stack) {
        usize slot_index = 0;
        for (const Value value : initial_locals) {
            auto stored = locals_.set(slot_index, value);
            if (!stored) {
                initialization_error_ = stored.error();
                break;
            }
            slot_index += value.category_two() ? 2 : 1;
        }
    }

    [[nodiscard]] const std::optional<Error>& initialization_error() const noexcept {
        return initialization_error_;
    }

    [[nodiscard]] usize pc() const noexcept { return pc_; }
    [[nodiscard]] usize code_size() const noexcept { return code_.size(); }

    [[nodiscard]] Result<u8> read_u8() {
        if (pc_ >= code_.size()) {
            return fail(ErrorCode::malformed_class,
                        "bytecode read exceeds method body");
        }
        return code_[pc_++];
    }

    [[nodiscard]] Result<i8> read_i8() {
        auto value = read_u8();
        if (!value) {
            return std::unexpected(value.error());
        }
        return static_cast<i8>(*value);
    }

    [[nodiscard]] Result<u16> read_u16() {
        auto high = read_u8();
        auto low = read_u8();
        if (!high || !low) {
            return fail(ErrorCode::malformed_class,
                        "truncated 16-bit bytecode operand");
        }
        return static_cast<u16>((static_cast<u16>(*high) << 8U) |
                                static_cast<u16>(*low));
    }

    [[nodiscard]] Result<i16> read_i16() {
        auto value = read_u16();
        if (!value) {
            return std::unexpected(value.error());
        }
        return static_cast<i16>(*value);
    }

    [[nodiscard]] Status branch(usize opcode_pc, i16 relative_offset) {
        const i64 target = static_cast<i64>(opcode_pc) +
                           static_cast<i64>(relative_offset);
        if (target < 0 || static_cast<u64>(target) >=
                              static_cast<u64>(code_.size())) {
            return fail(ErrorCode::malformed_class,
                        "bytecode branch target is out of range");
        }
        pc_ = static_cast<usize>(target);
        return {};
    }

    [[nodiscard]] Status push(Value value) { return stack_.push(value); }

    [[nodiscard]] Result<Value> pop() { return stack_.pop(); }

    [[nodiscard]] Result<i32> pop_int() {
        auto value = pop();
        if (!value) {
            return std::unexpected(value.error());
        }
        return value->as_int();
    }

    [[nodiscard]] Result<Value> local(usize index) const {
        return locals_.get(index);
    }

    [[nodiscard]] Status set_local(usize index, Value value) {
        return locals_.set(index, value);
    }

private:
    std::span<const u8> code_;
    LocalVariables locals_;
    OperandStack stack_;
    usize pc_ {0};
    std::optional<Error> initialization_error_;
};

[[nodiscard]] Result<i32> binary_int(Frame& frame, u8 opcode) {
    auto right = frame.pop_int();
    auto left = frame.pop_int();
    if (!right || !left) {
        return fail(ErrorCode::malformed_class,
                    "integer operation requires two int operands");
    }

    switch (opcode) {
    case 0x60:
        return static_cast<i32>(static_cast<u32>(*left) +
                                static_cast<u32>(*right));
    case 0x64:
        return static_cast<i32>(static_cast<u32>(*left) -
                                static_cast<u32>(*right));
    case 0x68:
        return static_cast<i32>(static_cast<u32>(*left) *
                                static_cast<u32>(*right));
    case 0x6C:
        if (*right == 0) {
            return fail(ErrorCode::invalid_state, "Java integer division by zero");
        }
        if (*left == std::numeric_limits<i32>::min() && *right == -1) {
            return std::numeric_limits<i32>::min();
        }
        return *left / *right;
    case 0x70:
        if (*right == 0) {
            return fail(ErrorCode::invalid_state, "Java integer remainder by zero");
        }
        if (*left == std::numeric_limits<i32>::min() && *right == -1) {
            return 0;
        }
        return *left % *right;
    case 0x7E:
        return *left & *right;
    case 0x80:
        return *left | *right;
    case 0x82:
        return *left ^ *right;
    default:
        return fail(ErrorCode::internal_error, "unknown integer operation");
    }
}

[[nodiscard]] bool test_zero(u8 opcode, i32 value) noexcept {
    switch (opcode) {
    case 0x99:
        return value == 0;
    case 0x9A:
        return value != 0;
    case 0x9B:
        return value < 0;
    case 0x9C:
        return value >= 0;
    case 0x9D:
        return value > 0;
    case 0x9E:
        return value <= 0;
    default:
        return false;
    }
}

[[nodiscard]] bool test_compare(u8 opcode, i32 left, i32 right) noexcept {
    switch (opcode) {
    case 0x9F:
        return left == right;
    case 0xA0:
        return left != right;
    case 0xA1:
        return left < right;
    case 0xA2:
        return left >= right;
    case 0xA3:
        return left > right;
    case 0xA4:
        return left <= right;
    default:
        return false;
    }
}

} // namespace

Result<ExecutionResult> Interpreter::execute(
    const classfile::CodeAttribute& code,
    std::span<const Value> initial_locals,
    u64 instruction_budget) const {
    usize initial_slots = 0;
    for (const Value value : initial_locals) {
        initial_slots += value.category_two() ? 2 : 1;
    }
    if (initial_slots > code.max_locals) {
        return fail(ErrorCode::invalid_argument,
                    "initial locals exceed method max_locals slots");
    }
    if (code.bytecode.empty()) {
        return fail(ErrorCode::malformed_class, "method has an empty Code attribute");
    }

    Frame frame(code, initial_locals);
    if (frame.initialization_error().has_value()) {
        return std::unexpected(*frame.initialization_error());
    }
    u64 executed = 0;

    while (frame.pc() < frame.code_size()) {
        if (executed >= instruction_budget) {
            return fail(ErrorCode::invalid_state,
                        "bytecode instruction budget was exhausted");
        }
        ++executed;

        const usize opcode_pc = frame.pc();
        auto opcode_value = frame.read_u8();
        if (!opcode_value) {
            return std::unexpected(opcode_value.error());
        }
        const u8 opcode = *opcode_value;

        switch (opcode) {
        case 0x00:
            break;
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x08: {
            const i32 constant = static_cast<i32>(opcode) - 3;
            auto pushed = frame.push(Value::from_int(constant));
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            break;
        }
        case 0x10: {
            auto immediate = frame.read_i8();
            if (!immediate) {
                return std::unexpected(immediate.error());
            }
            auto pushed = frame.push(Value::from_int(*immediate));
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            break;
        }
        case 0x11: {
            auto immediate = frame.read_i16();
            if (!immediate) {
                return std::unexpected(immediate.error());
            }
            auto pushed = frame.push(Value::from_int(*immediate));
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            break;
        }
        case 0x15: {
            auto index = frame.read_u8();
            if (!index) {
                return std::unexpected(index.error());
            }
            auto value = frame.local(*index);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto as_int = value->as_int();
            if (!as_int) {
                return std::unexpected(as_int.error());
            }
            auto pushed = frame.push(*value);
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            break;
        }
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D: {
            const usize index = static_cast<usize>(opcode - 0x1A);
            auto value = frame.local(index);
            if (!value) {
                return std::unexpected(value.error());
            }
            auto as_int = value->as_int();
            if (!as_int) {
                return std::unexpected(as_int.error());
            }
            auto pushed = frame.push(*value);
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            break;
        }
        case 0x36: {
            auto index = frame.read_u8();
            auto value = frame.pop();
            if (!index || !value) {
                return fail(ErrorCode::malformed_class,
                            "truncated or invalid istore instruction");
            }
            auto as_int = value->as_int();
            if (!as_int) {
                return std::unexpected(as_int.error());
            }
            auto stored = frame.set_local(*index, *value);
            if (!stored) {
                return std::unexpected(stored.error());
            }
            break;
        }
        case 0x3B:
        case 0x3C:
        case 0x3D:
        case 0x3E: {
            const usize index = static_cast<usize>(opcode - 0x3B);
            auto value = frame.pop();
            if (!value) {
                return std::unexpected(value.error());
            }
            auto as_int = value->as_int();
            if (!as_int) {
                return std::unexpected(as_int.error());
            }
            auto stored = frame.set_local(index, *value);
            if (!stored) {
                return std::unexpected(stored.error());
            }
            break;
        }
        case 0x57: {
            auto discarded = frame.pop();
            if (!discarded) {
                return std::unexpected(discarded.error());
            }
            if (discarded->category_two()) {
                return fail(ErrorCode::malformed_class,
                            "pop cannot consume a category-2 value");
            }
            break;
        }
        case 0x59: {
            auto value = frame.pop();
            if (!value) {
                return std::unexpected(value.error());
            }
            if (value->category_two()) {
                return fail(ErrorCode::malformed_class,
                            "dup cannot duplicate a category-2 value");
            }
            auto first = frame.push(*value);
            auto second = frame.push(*value);
            if (!first || !second) {
                return fail(ErrorCode::malformed_class,
                            "dup exceeds operand stack");
            }
            break;
        }
        case 0x60:
        case 0x64:
        case 0x68:
        case 0x6C:
        case 0x70:
        case 0x7E:
        case 0x80:
        case 0x82: {
            auto result = binary_int(frame, opcode);
            if (!result) {
                return std::unexpected(result.error());
            }
            auto pushed = frame.push(Value::from_int(*result));
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            break;
        }
        case 0x74: {
            auto value = frame.pop_int();
            if (!value) {
                return std::unexpected(value.error());
            }
            const i32 negated = static_cast<i32>(0U - static_cast<u32>(*value));
            auto pushed = frame.push(Value::from_int(negated));
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            break;
        }
        case 0x78:
        case 0x7A:
        case 0x7C: {
            auto distance = frame.pop_int();
            auto value = frame.pop_int();
            if (!distance || !value) {
                return fail(ErrorCode::malformed_class,
                            "shift requires two int operands");
            }
            const u32 shift = static_cast<u32>(*distance) & 0x1FU;
            i32 result = 0;
            if (opcode == 0x78) {
                result = static_cast<i32>(static_cast<u32>(*value) << shift);
            } else if (opcode == 0x7A) {
                result = *value >> shift;
            } else {
                result = static_cast<i32>(static_cast<u32>(*value) >> shift);
            }
            auto pushed = frame.push(Value::from_int(result));
            if (!pushed) {
                return std::unexpected(pushed.error());
            }
            break;
        }
        case 0x84: {
            auto index = frame.read_u8();
            auto increment = frame.read_i8();
            if (!index || !increment) {
                return fail(ErrorCode::malformed_class,
                            "truncated iinc instruction");
            }
            auto current = frame.local(*index);
            if (!current) {
                return std::unexpected(current.error());
            }
            auto integer = current->as_int();
            if (!integer) {
                return std::unexpected(integer.error());
            }
            const i32 updated = static_cast<i32>(
                static_cast<u32>(*integer) +
                static_cast<u32>(static_cast<i32>(*increment)));
            auto stored = frame.set_local(*index, Value::from_int(updated));
            if (!stored) {
                return std::unexpected(stored.error());
            }
            break;
        }
        case 0x99:
        case 0x9A:
        case 0x9B:
        case 0x9C:
        case 0x9D:
        case 0x9E: {
            auto offset = frame.read_i16();
            auto value = frame.pop_int();
            if (!offset || !value) {
                return fail(ErrorCode::malformed_class,
                            "invalid single-value conditional branch");
            }
            if (test_zero(opcode, *value)) {
                auto branched = frame.branch(opcode_pc, *offset);
                if (!branched) {
                    return std::unexpected(branched.error());
                }
            }
            break;
        }
        case 0x9F:
        case 0xA0:
        case 0xA1:
        case 0xA2:
        case 0xA3:
        case 0xA4: {
            auto offset = frame.read_i16();
            auto right = frame.pop_int();
            auto left = frame.pop_int();
            if (!offset || !right || !left) {
                return fail(ErrorCode::malformed_class,
                            "invalid integer comparison branch");
            }
            if (test_compare(opcode, *left, *right)) {
                auto branched = frame.branch(opcode_pc, *offset);
                if (!branched) {
                    return std::unexpected(branched.error());
                }
            }
            break;
        }
        case 0xA7: {
            auto offset = frame.read_i16();
            if (!offset) {
                return std::unexpected(offset.error());
            }
            auto branched = frame.branch(opcode_pc, *offset);
            if (!branched) {
                return std::unexpected(branched.error());
            }
            break;
        }
        case 0xAC: {
            auto result = frame.pop();
            if (!result) {
                return std::unexpected(result.error());
            }
            auto integer = result->as_int();
            if (!integer) {
                return std::unexpected(integer.error());
            }
            return ExecutionResult {
                .return_value = *result,
                .executed_instructions = executed,
            };
        }
        case 0xB1:
            return ExecutionResult {
                .return_value = std::nullopt,
                .executed_instructions = executed,
            };
        case 0xC4: {
            auto widened_opcode = frame.read_u8();
            auto index = frame.read_u16();
            if (!widened_opcode || !index) {
                return fail(ErrorCode::malformed_class,
                            "truncated wide instruction");
            }
            if (*widened_opcode == 0x15) {
                auto value = frame.local(*index);
                if (!value) {
                    return std::unexpected(value.error());
                }
                auto integer = value->as_int();
                if (!integer) {
                    return std::unexpected(integer.error());
                }
                auto pushed = frame.push(*value);
                if (!pushed) {
                    return std::unexpected(pushed.error());
                }
            } else if (*widened_opcode == 0x36) {
                auto value = frame.pop();
                if (!value) {
                    return std::unexpected(value.error());
                }
                auto integer = value->as_int();
                if (!integer) {
                    return std::unexpected(integer.error());
                }
                auto stored = frame.set_local(*index, *value);
                if (!stored) {
                    return std::unexpected(stored.error());
                }
            } else if (*widened_opcode == 0x84) {
                auto increment = frame.read_i16();
                if (!increment) {
                    return std::unexpected(increment.error());
                }
                auto current = frame.local(*index);
                if (!current) {
                    return std::unexpected(current.error());
                }
                auto integer = current->as_int();
                if (!integer) {
                    return std::unexpected(integer.error());
                }
                const i32 updated = static_cast<i32>(
                    static_cast<u32>(*integer) +
                    static_cast<u32>(static_cast<i32>(*increment)));
                auto stored = frame.set_local(*index, Value::from_int(updated));
                if (!stored) {
                    return std::unexpected(stored.error());
                }
            } else {
                return fail(ErrorCode::unsupported_feature,
                            "unsupported wide bytecode opcode");
            }
            break;
        }
        default:
            return fail(ErrorCode::unsupported_feature,
                        "bytecode opcode is not ported yet: " +
                            std::to_string(opcode));
        }
    }

    return fail(ErrorCode::malformed_class,
                "method execution reached the end without a return opcode");
}

} // namespace phoneme::vm
