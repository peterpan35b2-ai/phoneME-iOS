#include "phoneme/classfile/BytecodeVerifier.hpp"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "phoneme/base/ByteReader.hpp"
#include "phoneme/base/Checked.hpp"

namespace phoneme::classfile {
namespace {

struct BranchTarget final {
    usize origin {0};
    i64 relative_offset {0};
};

[[nodiscard]] Result<u8> read_u8(ByteReader& reader,
                                 std::string_view context) {
    auto value = reader.read_u8();
    if (!value) {
        return fail(ErrorCode::malformed_class,
                    "truncated bytecode operand: " + std::string(context));
    }
    return *value;
}

[[nodiscard]] Result<u16> read_u16(ByteReader& reader,
                                   std::string_view context) {
    auto value = reader.read_be_u16();
    if (!value) {
        return fail(ErrorCode::malformed_class,
                    "truncated bytecode operand: " + std::string(context));
    }
    return *value;
}

[[nodiscard]] Result<i16> read_i16(ByteReader& reader,
                                   std::string_view context) {
    auto value = read_u16(reader, context);
    if (!value) {
        return std::unexpected(value.error());
    }
    return static_cast<i16>(*value);
}

[[nodiscard]] Result<i32> read_i32(ByteReader& reader,
                                   std::string_view context) {
    auto value = reader.read_be_u32();
    if (!value) {
        return fail(ErrorCode::malformed_class,
                    "truncated bytecode operand: " + std::string(context));
    }
    return static_cast<i32>(*value);
}

[[nodiscard]] Status skip_bytes(ByteReader& reader,
                                usize count,
                                std::string_view context) {
    auto skipped = reader.skip(count);
    if (!skipped) {
        return fail(ErrorCode::malformed_class,
                    "truncated bytecode operand: " + std::string(context));
    }
    return {};
}

[[nodiscard]] bool has_no_operands(u8 opcode) noexcept {
    return opcode <= 0x0FU ||
           (opcode >= 0x1AU && opcode <= 0x35U) ||
           (opcode >= 0x3BU && opcode <= 0x83U) ||
           (opcode >= 0x85U && opcode <= 0x98U) ||
           (opcode >= 0xACU && opcode <= 0xB1U) ||
           opcode == 0xBEU || opcode == 0xBFU ||
           opcode == 0xC2U || opcode == 0xC3U;
}

void append_branch(std::vector<BranchTarget>& targets,
                   usize origin,
                   i64 offset) {
    targets.push_back(BranchTarget {
        .origin = origin,
        .relative_offset = offset,
    });
}

[[nodiscard]] Status read_switch_padding(ByteReader& reader) {
    // Some production J2ME obfuscators leave non-zero bytes in the alignment
    // gap before tableswitch/lookupswitch. The bytes are never executed and
    // phoneME-compatible runtimes ignore their contents, so only validate that
    // the aligned payload is still fully present.
    while ((reader.position() & 3U) != 0U) {
        auto padding = read_u8(reader, "switch padding");
        if (!padding) {
            return std::unexpected(padding.error());
        }
    }
    return {};
}

[[nodiscard]] Status decode_tableswitch(
    ByteReader& reader,
    usize origin,
    std::vector<BranchTarget>& targets) {
    auto padded = read_switch_padding(reader);
    if (!padded) {
        return std::unexpected(padded.error());
    }
    auto default_offset = read_i32(reader, "tableswitch default");
    auto low = read_i32(reader, "tableswitch low");
    auto high = read_i32(reader, "tableswitch high");
    if (!default_offset || !low || !high) {
        return fail(ErrorCode::malformed_class,
                    "truncated tableswitch header");
    }
    if (*high < *low) {
        return fail(ErrorCode::malformed_class,
                    "tableswitch high key is below low key");
    }
    append_branch(targets, origin, *default_offset);

    const i64 count64 = static_cast<i64>(*high) -
                        static_cast<i64>(*low) + 1;
    auto count = checked_narrow<usize>(count64);
    if (!count) {
        return fail(ErrorCode::malformed_class,
                    "tableswitch entry count overflows host size");
    }
    auto byte_count = checked_multiply(*count, static_cast<usize>(4));
    if (!byte_count || *byte_count > reader.remaining()) {
        return fail(ErrorCode::malformed_class,
                    "tableswitch entries exceed method bytecode");
    }
    for (usize index = 0; index < *count; ++index) {
        auto offset = read_i32(reader, "tableswitch target");
        if (!offset) {
            return std::unexpected(offset.error());
        }
        append_branch(targets, origin, *offset);
    }
    return {};
}

[[nodiscard]] Status decode_lookupswitch(
    ByteReader& reader,
    usize origin,
    std::vector<BranchTarget>& targets) {
    auto padded = read_switch_padding(reader);
    if (!padded) {
        return std::unexpected(padded.error());
    }
    auto default_offset = read_i32(reader, "lookupswitch default");
    auto pair_count = read_i32(reader, "lookupswitch pair count");
    if (!default_offset || !pair_count || *pair_count < 0) {
        return fail(ErrorCode::malformed_class,
                    "invalid lookupswitch header");
    }
    append_branch(targets, origin, *default_offset);

    auto count = checked_narrow<usize>(*pair_count);
    if (!count) {
        return fail(ErrorCode::malformed_class,
                    "lookupswitch pair count overflows host size");
    }
    auto byte_count = checked_multiply(*count, static_cast<usize>(8));
    if (!byte_count || *byte_count > reader.remaining()) {
        return fail(ErrorCode::malformed_class,
                    "lookupswitch pairs exceed method bytecode");
    }

    std::optional<i32> previous_match;
    for (usize index = 0; index < *count; ++index) {
        auto match = read_i32(reader, "lookupswitch match");
        auto offset = read_i32(reader, "lookupswitch target");
        if (!match || !offset) {
            return fail(ErrorCode::malformed_class,
                        "truncated lookupswitch pair");
        }
        if (previous_match.has_value() && *match <= *previous_match) {
            return fail(ErrorCode::malformed_class,
                        "lookupswitch keys must be strictly increasing");
        }
        previous_match = *match;
        append_branch(targets, origin, *offset);
    }
    return {};
}

[[nodiscard]] Status decode_wide(ByteReader& reader) {
    auto widened_opcode = read_u8(reader, "wide opcode");
    if (!widened_opcode) {
        return std::unexpected(widened_opcode.error());
    }
    switch (*widened_opcode) {
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x19:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
    case 0x3A:
    case 0xA9:
        return skip_bytes(reader, 2, "wide local index");
    case 0x84:
        return skip_bytes(reader, 4, "wide iinc operands");
    default:
        return fail(ErrorCode::malformed_class,
                    "wide modifies an invalid opcode");
    }
}

[[nodiscard]] Status validate_branch_targets(
    std::span<const u8> instruction_starts,
    std::span<const BranchTarget> targets,
    usize code_size) {
    for (const BranchTarget& branch : targets) {
        const i64 origin = static_cast<i64>(branch.origin);
        if ((branch.relative_offset > 0 &&
             origin > std::numeric_limits<i64>::max() -
                          branch.relative_offset) ||
            (branch.relative_offset < 0 &&
             origin < std::numeric_limits<i64>::min() -
                          branch.relative_offset)) {
            return fail(ErrorCode::malformed_class,
                        "bytecode branch target calculation overflowed");
        }
        const i64 target64 = origin + branch.relative_offset;
        if (target64 < 0 ||
            static_cast<u64>(target64) >= static_cast<u64>(code_size)) {
            return fail(ErrorCode::malformed_class,
                        "bytecode branch target is outside method body");
        }
        const usize target = static_cast<usize>(target64);
        if (!instruction_starts[target]) {
            return fail(ErrorCode::malformed_class,
                        "bytecode branch target is not an instruction boundary");
        }
    }
    return {};
}

} // namespace

Status verify_code_structure(
    std::span<const u8> bytecode,
    std::span<const ExceptionHandler> exception_table,
    std::span<const StackMapFrame> stack_map_frames) {
    if (bytecode.empty()) {
        return fail(ErrorCode::malformed_class,
                    "Code attribute contains no bytecode");
    }

    ByteReader reader(bytecode);
    std::vector<u8> instruction_starts(bytecode.size() + 1, 0U);
    std::vector<BranchTarget> branch_targets;
    branch_targets.reserve(16);

    while (!reader.empty()) {
        const usize opcode_pc = reader.position();
        instruction_starts[opcode_pc] = 1U;
        auto opcode_result = read_u8(reader, "opcode");
        if (!opcode_result) {
            return std::unexpected(opcode_result.error());
        }
        const u8 opcode = *opcode_result;

        if (has_no_operands(opcode)) {
            continue;
        }
        if (opcode == 0x10U || opcode == 0x12U ||
            (opcode >= 0x15U && opcode <= 0x19U) ||
            (opcode >= 0x36U && opcode <= 0x3AU) ||
            opcode == 0xA9U) {
            auto skipped = skip_bytes(reader, 1, "single-byte operand");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if (opcode == 0x11U || opcode == 0x13U || opcode == 0x14U ||
            opcode == 0x84U ||
            (opcode >= 0xB2U && opcode <= 0xB8U) ||
            opcode == 0xBBU || opcode == 0xBDU ||
            opcode == 0xC0U || opcode == 0xC1U) {
            auto skipped = skip_bytes(reader, 2, "two-byte operand");
            if (!skipped) return std::unexpected(skipped.error());
            continue;
        }
        if ((opcode >= 0x99U && opcode <= 0xA8U) ||
            opcode == 0xC6U || opcode == 0xC7U) {
            auto offset = read_i16(reader, "short branch offset");
            if (!offset) return std::unexpected(offset.error());
            append_branch(branch_targets, opcode_pc, *offset);
            continue;
        }
        if (opcode == 0xAAU) {
            auto decoded = decode_tableswitch(reader,
                                              opcode_pc,
                                              branch_targets);
            if (!decoded) return std::unexpected(decoded.error());
            continue;
        }
        if (opcode == 0xABU) {
            auto decoded = decode_lookupswitch(reader,
                                               opcode_pc,
                                               branch_targets);
            if (!decoded) return std::unexpected(decoded.error());
            continue;
        }
        if (opcode == 0xB9U) {
            auto index = read_u16(reader, "invokeinterface index");
            auto count = read_u8(reader, "invokeinterface count");
            auto zero = read_u8(reader, "invokeinterface zero");
            if (!index || !count || !zero || *count == 0U || *zero != 0U) {
                return fail(ErrorCode::malformed_class,
                            "invalid invokeinterface operands");
            }
            continue;
        }
        if (opcode == 0xBAU) {
            auto index = read_u16(reader, "invokedynamic index");
            auto zero1 = read_u8(reader, "invokedynamic zero");
            auto zero2 = read_u8(reader, "invokedynamic zero");
            if (!index || !zero1 || !zero2 || *zero1 != 0U || *zero2 != 0U) {
                return fail(ErrorCode::malformed_class,
                            "invalid invokedynamic operands");
            }
            continue;
        }
        if (opcode == 0xBCU) {
            auto atype = read_u8(reader, "newarray type");
            if (!atype || *atype < 4U || *atype > 11U) {
                return fail(ErrorCode::malformed_class,
                            "newarray contains an invalid primitive type");
            }
            continue;
        }
        if (opcode == 0xC4U) {
            auto decoded = decode_wide(reader);
            if (!decoded) return std::unexpected(decoded.error());
            continue;
        }
        if (opcode == 0xC5U) {
            auto index = read_u16(reader, "multianewarray index");
            auto dimensions = read_u8(reader, "multianewarray dimensions");
            if (!index || !dimensions || *dimensions == 0U) {
                return fail(ErrorCode::malformed_class,
                            "invalid multianewarray operands");
            }
            continue;
        }
        if (opcode == 0xC8U || opcode == 0xC9U) {
            auto offset = read_i32(reader, "wide branch offset");
            if (!offset) return std::unexpected(offset.error());
            append_branch(branch_targets, opcode_pc, *offset);
            continue;
        }

        return fail(ErrorCode::malformed_class,
                    "class contains an invalid or reserved bytecode opcode");
    }

    instruction_starts[bytecode.size()] = 1U;
    auto branches_valid = validate_branch_targets(instruction_starts,
                                                  branch_targets,
                                                  bytecode.size());
    if (!branches_valid) {
        return std::unexpected(branches_valid.error());
    }

    for (const ExceptionHandler& handler : exception_table) {
        const usize start = static_cast<usize>(handler.start_pc);
        const usize end = static_cast<usize>(handler.end_pc);
        const usize target = static_cast<usize>(handler.handler_pc);
        if (start >= end || end > bytecode.size() ||
            target >= bytecode.size()) {
            return fail(ErrorCode::malformed_class,
                        "exception handler range or target is outside bytecode");
        }
        if (!instruction_starts[start] || !instruction_starts[end] ||
            !instruction_starts[target]) {
            return fail(ErrorCode::malformed_class,
                        "exception handler is not aligned to instruction boundaries");
        }
    }

    std::optional<usize> previous_stack_map_offset;
    const auto validate_verification_type =
        [&bytecode, &instruction_starts](const VerificationType& type)
        -> Status {
        if (type.kind == VerificationTypeKind::object &&
            type.class_name.empty()) {
            return fail(ErrorCode::malformed_class,
                        "stack map object type has no class name");
        }
        if (type.kind == VerificationTypeKind::uninitialized) {
            const usize new_pc = static_cast<usize>(type.new_instruction_pc);
            if (new_pc >= bytecode.size() || !instruction_starts[new_pc] ||
                bytecode[new_pc] != 0xBBU) {
                return fail(ErrorCode::malformed_class,
                            "stack map uninitialized type does not reference new");
            }
        }
        return {};
    };

    for (const StackMapFrame& frame : stack_map_frames) {
        // CLDC StackMap is optional preverification metadata, not executable
        // bytecode. Many commercial MIDlets were post-processed or patched
        // after preverification and therefore contain stale offsets. The full
        // VM verifier independently performs bytecode data-flow validation and
        // already retries legacy methods without these hints. Keep validating
        // each frame's payload and types, but reserve strict offset/alignment
        // checks for modern StackMapTable metadata.
        const bool legacy_cldc = frame.kind == StackMapFrameKind::cldc_full;
        const usize offset = static_cast<usize>(frame.bytecode_offset);
        if (!legacy_cldc &&
            (offset >= bytecode.size() || !instruction_starts[offset])) {
            return fail(ErrorCode::malformed_class,
                        "stack map frame is not on an instruction boundary");
        }
        if (!legacy_cldc && previous_stack_map_offset.has_value() &&
            offset <= *previous_stack_map_offset) {
            return fail(ErrorCode::malformed_class,
                        "stack map frame offsets are not strictly increasing");
        }
        if (!legacy_cldc) previous_stack_map_offset = offset;

        switch (frame.kind) {
        case StackMapFrameKind::same:
            if (!frame.locals.empty() || !frame.stack.empty() ||
                frame.chopped_locals != 0U) {
                return fail(ErrorCode::malformed_class,
                            "same stack map frame contains unexpected payload");
            }
            break;
        case StackMapFrameKind::same_locals_one_stack:
            if (!frame.locals.empty() || frame.stack.size() != 1U ||
                frame.chopped_locals != 0U) {
                return fail(ErrorCode::malformed_class,
                            "same-locals stack map frame payload is invalid");
            }
            break;
        case StackMapFrameKind::chop:
            if (!frame.locals.empty() || !frame.stack.empty() ||
                frame.chopped_locals == 0U || frame.chopped_locals > 3U) {
                return fail(ErrorCode::malformed_class,
                            "chop stack map frame payload is invalid");
            }
            break;
        case StackMapFrameKind::append:
            if (frame.locals.empty() || frame.locals.size() > 3U ||
                !frame.stack.empty() || frame.chopped_locals != 0U) {
                return fail(ErrorCode::malformed_class,
                            "append stack map frame payload is invalid");
            }
            break;
        case StackMapFrameKind::cldc_full:
        case StackMapFrameKind::full:
            if (frame.chopped_locals != 0U) {
                return fail(ErrorCode::malformed_class,
                            "full stack map frame has a chop count");
            }
            break;
        }

        for (const VerificationType& local : frame.locals) {
            auto valid = validate_verification_type(local);
            if (!valid) return std::unexpected(valid.error());
        }
        for (const VerificationType& stack_type : frame.stack) {
            auto valid = validate_verification_type(stack_type);
            if (!valid) return std::unexpected(valid.error());
        }
    }
    return {};
}

} // namespace phoneme::classfile
