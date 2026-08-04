#include "phoneme/vm/DecodedMethod.hpp"

#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include "phoneme/base/Checked.hpp"
#include "phoneme/classfile/BytecodeVerifier.hpp"
#include "phoneme/vm/PerformanceCounters.hpp"

namespace phoneme::vm {
namespace {

class DecodeCursor final {
public:
    explicit DecodeCursor(std::span<const u8> bytes) noexcept : bytes_(bytes) {}

    [[nodiscard]] usize position() const noexcept { return position_; }
    [[nodiscard]] bool empty() const noexcept { return position_ == bytes_.size(); }

    [[nodiscard]] Result<u8> read_u8(std::string_view context) {
        if (position_ >= bytes_.size()) {
            return fail(ErrorCode::malformed_class,
                        "truncated decoded bytecode operand: " +
                            std::string(context));
        }
        return bytes_[position_++];
    }

    [[nodiscard]] Result<u16> read_u16(std::string_view context) {
        auto high = read_u8(context);
        auto low = read_u8(context);
        if (!high || !low) {
            return fail(ErrorCode::malformed_class,
                        "truncated decoded 16-bit operand: " +
                            std::string(context));
        }
        return static_cast<u16>((static_cast<u16>(*high) << 8U) |
                                static_cast<u16>(*low));
    }

    [[nodiscard]] Result<i16> read_i16(std::string_view context) {
        auto value = read_u16(context);
        if (!value) return std::unexpected(value.error());
        return static_cast<i16>(*value);
    }

    [[nodiscard]] Result<i32> read_i32(std::string_view context) {
        auto first = read_u8(context);
        auto second = read_u8(context);
        auto third = read_u8(context);
        auto fourth = read_u8(context);
        if (!first || !second || !third || !fourth) {
            return fail(ErrorCode::malformed_class,
                        "truncated decoded 32-bit operand: " +
                            std::string(context));
        }
        const u32 bits = (static_cast<u32>(*first) << 24U) |
                         (static_cast<u32>(*second) << 16U) |
                         (static_cast<u32>(*third) << 8U) |
                         static_cast<u32>(*fourth);
        return static_cast<i32>(bits);
    }

    [[nodiscard]] Status skip_switch_padding() {
        while ((position_ & 3U) != 0U) {
            auto ignored = read_u8("switch padding");
            if (!ignored) return std::unexpected(ignored.error());
        }
        return {};
    }

private:
    std::span<const u8> bytes_;
    usize position_ {0};
};

[[nodiscard]] bool has_no_operands(u8 opcode) noexcept {
    return opcode <= 0x0FU ||
           (opcode >= 0x1AU && opcode <= 0x35U) ||
           (opcode >= 0x3BU && opcode <= 0x83U) ||
           (opcode >= 0x85U && opcode <= 0x98U) ||
           (opcode >= 0xACU && opcode <= 0xB1U) ||
           opcode == 0xBEU || opcode == 0xBFU ||
           opcode == 0xC2U || opcode == 0xC3U;
}

[[nodiscard]] Result<u32> narrow_index(usize value,
                                       std::string_view context) {
    auto narrowed = checked_narrow<u32>(value);
    if (!narrowed) {
        return fail(ErrorCode::overflow,
                    std::string(context) + " exceeds decoded index space");
    }
    return *narrowed;
}

[[nodiscard]] Result<u32> branch_target(usize origin,
                                        i64 relative,
                                        usize code_size) {
    const i64 origin64 = static_cast<i64>(origin);
    if ((relative > 0 &&
         origin64 > std::numeric_limits<i64>::max() - relative) ||
        (relative < 0 &&
         origin64 < std::numeric_limits<i64>::min() - relative)) {
        return fail(ErrorCode::malformed_class,
                    "decoded branch target calculation overflowed");
    }
    const i64 target = origin64 + relative;
    if (target < 0 || static_cast<u64>(target) >= code_size) {
        return fail(ErrorCode::malformed_class,
                    "decoded branch target is outside method body");
    }
    return static_cast<u32>(target);
}

[[nodiscard]] Result<u32> append_operand(DecodedMethod& decoded,
                                         DecodedOperand operand) {
    auto index = narrow_index(decoded.operands.size(), "decoded operand table");
    if (!index) return std::unexpected(index.error());
    decoded.operands.push_back(std::move(operand));
    return *index;
}

[[nodiscard]] Result<u32> append_switch(DecodedMethod& decoded,
                                        DecodedSwitchTable table) {
    auto index = narrow_index(decoded.switches.size(), "decoded switch table");
    if (!index) return std::unexpected(index.error());
    decoded.switches.push_back(std::move(table));
    return *index;
}

[[nodiscard]] Status resolve_target(const DecodedMethod& decoded,
                                    u32 target_bci,
                                    u32& target_index) {
    if (target_bci >= decoded.bci_to_instruction.size()) {
        return fail(ErrorCode::malformed_class,
                    "decoded target BCI exceeds method body");
    }
    target_index = decoded.bci_to_instruction[target_bci];
    if (target_index == kInvalidDecodedIndex ||
        target_index >= decoded.instructions.size()) {
        return fail(ErrorCode::malformed_class,
                    "decoded target BCI is not an instruction boundary");
    }
    return {};
}

} // namespace

u32 DecodedMethod::instruction_index_for_bci(u32 bci) const noexcept {
    if (bci >= bci_to_instruction.size()) return kInvalidDecodedIndex;
    return bci_to_instruction[bci];
}

Result<DecodedMethod> decode_method(MethodId method_id,
                                    const classfile::Method& method) {
    if (!method.code.has_value()) {
        return fail(ErrorCode::invalid_argument,
                    "cannot decode a native or abstract method");
    }
    return decode_code(method_id, *method.code);
}

Result<DecodedMethod> decode_code(MethodId method_id,
                                  const classfile::CodeAttribute& code) {
    auto verified = classfile::verify_code_structure(
        code.bytecode, code.exception_table, code.stack_map_frames);
    if (!verified) return std::unexpected(verified.error());

    auto code_size = narrow_index(code.bytecode.size(), "method bytecode");
    if (!code_size) return std::unexpected(code_size.error());

    DecodedMethod decoded {
        .method_id = method_id,
        .original_bytecode_size = *code_size,
        .instructions = {},
        .bci_to_instruction = std::vector<u32>(
            code.bytecode.size() + 1U, kInvalidDecodedIndex),
        .operands = {},
        .switches = {},
        .exception_handlers = {},
    };
    decoded.instructions.reserve(code.bytecode.size());
    decoded.operands.reserve(code.bytecode.size() / 2U);

    DecodeCursor cursor(code.bytecode);
    while (!cursor.empty()) {
        const usize opcode_pc = cursor.position();
        auto instruction_index = narrow_index(
            decoded.instructions.size(), "decoded instruction table");
        auto opcode = cursor.read_u8("opcode");
        if (!instruction_index || !opcode) {
            return instruction_index
                ? std::unexpected(opcode.error())
                : std::unexpected(instruction_index.error());
        }
        decoded.bci_to_instruction[opcode_pc] = *instruction_index;
        DecodedInstruction instruction {
            .opcode = decode_raw_opcode(*opcode),
            .bytecode_pc = static_cast<u32>(opcode_pc),
            .next_index = kInvalidDecodedIndex,
            .operand_index = kInvalidDecodedIndex,
        };

        std::optional<DecodedOperand> operand;
        if (has_no_operands(*opcode)) {
            // No operand.
        } else if (*opcode == 0x10U) {
            auto immediate = cursor.read_u8("bipush immediate");
            if (!immediate) return std::unexpected(immediate.error());
            operand = DecodedOperand {
                .kind = DecodedOperandKind::immediate,
                .immediate = static_cast<i8>(*immediate),
            };
        } else if (*opcode == 0x11U) {
            auto immediate = cursor.read_i16("sipush immediate");
            if (!immediate) return std::unexpected(immediate.error());
            operand = DecodedOperand {
                .kind = DecodedOperandKind::immediate,
                .immediate = *immediate,
            };
        } else if (*opcode == 0x12U) {
            auto index = cursor.read_u8("ldc index");
            if (!index) return std::unexpected(index.error());
            operand = DecodedOperand {
                .kind = DecodedOperandKind::constant_pool_index,
                .constant_pool_index = *index,
            };
        } else if ((*opcode >= 0x15U && *opcode <= 0x19U) ||
                   (*opcode >= 0x36U && *opcode <= 0x3AU) ||
                   *opcode == 0xA9U) {
            auto index = cursor.read_u8("local index");
            if (!index) return std::unexpected(index.error());
            operand = DecodedOperand {
                .kind = DecodedOperandKind::local_index,
                .local_index = *index,
            };
        } else if (*opcode == 0x13U || *opcode == 0x14U ||
                   (*opcode >= 0xB2U && *opcode <= 0xB8U) ||
                   *opcode == 0xBBU || *opcode == 0xBDU ||
                   *opcode == 0xC0U || *opcode == 0xC1U) {
            auto index = cursor.read_u16("constant-pool index");
            if (!index) return std::unexpected(index.error());
            operand = DecodedOperand {
                .kind = DecodedOperandKind::constant_pool_index,
                .constant_pool_index = *index,
            };
        } else if (*opcode == 0x84U) {
            auto index = cursor.read_u8("iinc local index");
            auto increment = cursor.read_u8("iinc increment");
            if (!index || !increment) {
                return fail(ErrorCode::malformed_class,
                            "truncated decoded iinc operands");
            }
            operand = DecodedOperand {
                .kind = DecodedOperandKind::increment,
                .local_index = *index,
                .immediate = static_cast<i8>(*increment),
            };
        } else if ((*opcode >= 0x99U && *opcode <= 0xA8U) ||
                   *opcode == 0xC6U || *opcode == 0xC7U) {
            auto offset = cursor.read_i16("short branch offset");
            if (!offset) return std::unexpected(offset.error());
            auto target = branch_target(opcode_pc, *offset, code.bytecode.size());
            if (!target) return std::unexpected(target.error());
            operand = DecodedOperand {
                .kind = DecodedOperandKind::branch_target,
                .immediate = *offset,
                .target_bci = *target,
            };
        } else if (*opcode == 0xAAU || *opcode == 0xABU) {
            auto padded = cursor.skip_switch_padding();
            if (!padded) return std::unexpected(padded.error());
            auto default_offset = cursor.read_i32("switch default offset");
            if (!default_offset) return std::unexpected(default_offset.error());
            auto default_target = branch_target(
                opcode_pc, *default_offset, code.bytecode.size());
            if (!default_target) return std::unexpected(default_target.error());

            DecodedSwitchTable table {
                .lookup = *opcode == 0xABU,
                .low = 0,
                .high = -1,
                .default_target_bci = *default_target,
                .default_target_index = kInvalidDecodedIndex,
                .entries = {},
            };
            if (*opcode == 0xAAU) {
                auto low = cursor.read_i32("tableswitch low");
                auto high = cursor.read_i32("tableswitch high");
                if (!low || !high || *high < *low) {
                    return fail(ErrorCode::malformed_class,
                                "invalid decoded tableswitch range");
                }
                table.low = *low;
                table.high = *high;
                const i64 count64 = static_cast<i64>(*high) -
                                    static_cast<i64>(*low) + 1;
                auto count = checked_narrow<usize>(count64);
                if (!count) return std::unexpected(count.error());
                table.entries.reserve(*count);
                for (usize index = 0; index < *count; ++index) {
                    auto offset = cursor.read_i32("tableswitch target");
                    if (!offset) return std::unexpected(offset.error());
                    auto target = branch_target(
                        opcode_pc, *offset, code.bytecode.size());
                    if (!target) return std::unexpected(target.error());
                    table.entries.push_back(DecodedSwitchEntry {
                        .match = static_cast<i32>(
                            static_cast<i64>(*low) + static_cast<i64>(index)),
                        .target_bci = *target,
                        .target_index = kInvalidDecodedIndex,
                    });
                }
            } else {
                auto pair_count = cursor.read_i32("lookupswitch pair count");
                if (!pair_count || *pair_count < 0) {
                    return fail(ErrorCode::malformed_class,
                                "invalid decoded lookupswitch pair count");
                }
                auto count = checked_narrow<usize>(*pair_count);
                if (!count) return std::unexpected(count.error());
                table.entries.reserve(*count);
                std::optional<i32> previous_match;
                for (usize index = 0; index < *count; ++index) {
                    auto match = cursor.read_i32("lookupswitch match");
                    auto offset = cursor.read_i32("lookupswitch target");
                    if (!match || !offset) {
                        return fail(ErrorCode::malformed_class,
                                    "truncated decoded lookupswitch pair");
                    }
                    if (previous_match.has_value() &&
                        *match <= *previous_match) {
                        return fail(ErrorCode::malformed_class,
                                    "decoded lookupswitch keys are not ordered");
                    }
                    previous_match = *match;
                    auto target = branch_target(
                        opcode_pc, *offset, code.bytecode.size());
                    if (!target) return std::unexpected(target.error());
                    table.entries.push_back(DecodedSwitchEntry {
                        .match = *match,
                        .target_bci = *target,
                        .target_index = kInvalidDecodedIndex,
                    });
                }
            }
            auto switch_index = append_switch(decoded, std::move(table));
            if (!switch_index) return std::unexpected(switch_index.error());
            operand = DecodedOperand {
                .kind = DecodedOperandKind::switch_table,
                .switch_index = *switch_index,
            };
        } else if (*opcode == 0xB9U) {
            auto index = cursor.read_u16("invokeinterface index");
            auto count = cursor.read_u8("invokeinterface count");
            auto zero = cursor.read_u8("invokeinterface zero");
            if (!index || !count || !zero || *count == 0U || *zero != 0U) {
                return fail(ErrorCode::malformed_class,
                            "invalid decoded invokeinterface operands");
            }
            operand = DecodedOperand {
                .kind = DecodedOperandKind::invokeinterface,
                .constant_pool_index = *index,
                .auxiliary = *count,
            };
        } else if (*opcode == 0xBAU) {
            auto index = cursor.read_u16("invokedynamic index");
            auto zero1 = cursor.read_u8("invokedynamic zero");
            auto zero2 = cursor.read_u8("invokedynamic zero");
            if (!index || !zero1 || !zero2 || *zero1 != 0U || *zero2 != 0U) {
                return fail(ErrorCode::malformed_class,
                            "invalid decoded invokedynamic operands");
            }
            operand = DecodedOperand {
                .kind = DecodedOperandKind::invokedynamic,
                .constant_pool_index = *index,
            };
        } else if (*opcode == 0xBCU) {
            auto array_type = cursor.read_u8("newarray type");
            if (!array_type || *array_type < 4U || *array_type > 11U) {
                return fail(ErrorCode::malformed_class,
                            "invalid decoded newarray type");
            }
            operand = DecodedOperand {
                .kind = DecodedOperandKind::newarray_type,
                .auxiliary = *array_type,
            };
        } else if (*opcode == 0xC4U) {
            auto modified = cursor.read_u8("wide opcode");
            auto local = cursor.read_u16("wide local index");
            if (!modified || !local) {
                return fail(ErrorCode::malformed_class,
                            "truncated decoded wide operands");
            }
            if (*modified == 0x84U) {
                auto increment = cursor.read_i16("wide iinc increment");
                if (!increment) return std::unexpected(increment.error());
                operand = DecodedOperand {
                    .kind = DecodedOperandKind::wide_increment,
                    .local_index = *local,
                    .immediate = *increment,
                    .modified_opcode = *modified,
                };
            } else {
                const bool valid = (*modified >= 0x15U && *modified <= 0x19U) ||
                                   (*modified >= 0x36U && *modified <= 0x3AU) ||
                                   *modified == 0xA9U;
                if (!valid) {
                    return fail(ErrorCode::malformed_class,
                                "wide modifies an invalid decoded opcode");
                }
                operand = DecodedOperand {
                    .kind = DecodedOperandKind::wide_local,
                    .local_index = *local,
                    .modified_opcode = *modified,
                };
            }
        } else if (*opcode == 0xC5U) {
            auto index = cursor.read_u16("multianewarray index");
            auto dimensions = cursor.read_u8("multianewarray dimensions");
            if (!index || !dimensions || *dimensions == 0U) {
                return fail(ErrorCode::malformed_class,
                            "invalid decoded multianewarray operands");
            }
            operand = DecodedOperand {
                .kind = DecodedOperandKind::multianewarray,
                .constant_pool_index = *index,
                .auxiliary = *dimensions,
            };
        } else if (*opcode == 0xC8U || *opcode == 0xC9U) {
            auto offset = cursor.read_i32("wide branch offset");
            if (!offset) return std::unexpected(offset.error());
            auto target = branch_target(opcode_pc, *offset, code.bytecode.size());
            if (!target) return std::unexpected(target.error());
            operand = DecodedOperand {
                .kind = DecodedOperandKind::branch_target,
                .immediate = *offset,
                .target_bci = *target,
            };
        } else {
            return fail(ErrorCode::malformed_class,
                        "decoder encountered an invalid or reserved opcode");
        }

        if (operand.has_value()) {
            auto operand_index = append_operand(decoded, std::move(*operand));
            if (!operand_index) return std::unexpected(operand_index.error());
            instruction.operand_index = *operand_index;
        }
        decoded.instructions.push_back(instruction);
    }

    decoded.bci_to_instruction[code.bytecode.size()] =
        static_cast<u32>(decoded.instructions.size());
    for (usize index = 0; index < decoded.instructions.size(); ++index) {
        decoded.instructions[index].next_index = static_cast<u32>(index + 1U);
    }

    for (DecodedOperand& operand : decoded.operands) {
        if (operand.kind == DecodedOperandKind::branch_target) {
            auto resolved = resolve_target(
                decoded, operand.target_bci, operand.target_index);
            if (!resolved) return std::unexpected(resolved.error());
        } else if (operand.kind == DecodedOperandKind::switch_table) {
            if (operand.switch_index >= decoded.switches.size()) {
                return fail(ErrorCode::internal_error,
                            "decoded switch operand has an invalid table index");
            }
            DecodedSwitchTable& table = decoded.switches[operand.switch_index];
            auto default_resolved = resolve_target(
                decoded,
                table.default_target_bci,
                table.default_target_index);
            if (!default_resolved) {
                return std::unexpected(default_resolved.error());
            }
            for (DecodedSwitchEntry& entry : table.entries) {
                auto resolved = resolve_target(
                    decoded, entry.target_bci, entry.target_index);
                if (!resolved) return std::unexpected(resolved.error());
            }
        }
    }

    decoded.exception_handlers.reserve(code.exception_table.size());
    for (const classfile::ExceptionHandler& handler : code.exception_table) {
        const u32 start_bci = handler.start_pc;
        const u32 end_bci = handler.end_pc;
        const u32 handler_bci = handler.handler_pc;
        const u32 start_index = decoded.instruction_index_for_bci(start_bci);
        const u32 end_index = decoded.instruction_index_for_bci(end_bci);
        const u32 handler_index = decoded.instruction_index_for_bci(handler_bci);
        if (start_index == kInvalidDecodedIndex ||
            end_index == kInvalidDecodedIndex ||
            handler_index == kInvalidDecodedIndex ||
            handler_index >= decoded.instructions.size()) {
            return fail(ErrorCode::malformed_class,
                        "decoded exception handler is not instruction-aligned");
        }
        decoded.exception_handlers.push_back(DecodedExceptionHandler {
            .start_bci = start_bci,
            .end_bci = end_bci,
            .handler_bci = handler_bci,
            .start_index = start_index,
            .end_index = end_index,
            .handler_index = handler_index,
            .catch_type = handler.catch_type,
        });
    }

    usize switch_entries = 0U;
    for (const DecodedSwitchTable& table : decoded.switches) {
        switch_entries += table.entries.size();
    }
    PerformanceCounters::record_decoded_method(
        decoded.instructions.size(),
        decoded.operands.size(),
        switch_entries);
    return decoded;
}

} // namespace phoneme::vm
