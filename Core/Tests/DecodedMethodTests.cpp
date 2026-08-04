#include <cstdlib>
#include <iostream>
#include <vector>

#include "phoneme/vm/DecodedMethod.hpp"

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}

void append_i32(std::vector<phoneme::u8>& bytes, phoneme::i32 value) {
    const auto bits = static_cast<phoneme::u32>(value);
    bytes.push_back(static_cast<phoneme::u8>(bits >> 24U));
    bytes.push_back(static_cast<phoneme::u8>(bits >> 16U));
    bytes.push_back(static_cast<phoneme::u8>(bits >> 8U));
    bytes.push_back(static_cast<phoneme::u8>(bits));
}

void test_branch_mapping() {
    phoneme::classfile::CodeAttribute code {
        .max_stack = 1,
        .max_locals = 0,
        .bytecode = {0x03U, 0x99U, 0x00U, 0x04U, 0x04U, 0xACU},
    };
    auto decoded = phoneme::vm::decode_code(
        phoneme::vm::MethodId {1U}, code);
    require(decoded.has_value(), "decode short branch method");
    require(decoded->instructions.size() == 4U,
            "one decoded instruction per executable opcode");
    require(decoded->instruction_index_for_bci(0U) == 0U &&
                decoded->instruction_index_for_bci(1U) == 1U &&
                decoded->instruction_index_for_bci(4U) == 2U &&
                decoded->instruction_index_for_bci(5U) == 3U,
            "BCI map preserves sparse operand bytes");
    require(decoded->instruction_index_for_bci(2U) ==
                phoneme::vm::kInvalidDecodedIndex,
            "operand byte is not an instruction boundary");
    const auto& branch_instruction = decoded->instructions[1U];
    require(branch_instruction.operand_index < decoded->operands.size(),
            "branch has decoded operand");
    const auto& branch = decoded->operands[branch_instruction.operand_index];
    require(branch.kind == phoneme::vm::DecodedOperandKind::branch_target &&
                branch.target_bci == 5U && branch.target_index == 3U,
            "branch target resolves to decoded instruction index");
    require(decoded->instructions.back().next_index ==
                decoded->instructions.size(),
            "last decoded instruction points to method end sentinel");
}

void test_tableswitch_padding_and_targets() {
    std::vector<phoneme::u8> bytecode {0x03U, 0xAAU, 0xA5U, 0x5AU};
    append_i32(bytecode, 19); // default -> BCI 20
    append_i32(bytecode, 0);  // low
    append_i32(bytecode, 0);  // high
    append_i32(bytecode, 19); // case 0 -> BCI 20
    bytecode.push_back(0x04U);
    bytecode.push_back(0xACU);

    phoneme::classfile::CodeAttribute code {
        .max_stack = 1,
        .max_locals = 0,
        .bytecode = std::move(bytecode),
    };
    auto decoded = phoneme::vm::decode_code(
        phoneme::vm::MethodId {2U}, code);
    require(decoded.has_value(), "decode tableswitch with arbitrary padding");
    require(decoded->instructions.size() == 4U &&
                decoded->instructions[1U].bytecode_pc == 1U,
            "tableswitch remains one decoded instruction");
    const auto& operand = decoded->operands[
        decoded->instructions[1U].operand_index];
    require(operand.kind == phoneme::vm::DecodedOperandKind::switch_table &&
                operand.switch_index < decoded->switches.size(),
            "tableswitch points to side table");
    const auto& table = decoded->switches[operand.switch_index];
    require(!table.lookup && table.low == 0 && table.high == 0 &&
                table.default_target_bci == 20U &&
                table.default_target_index == 2U &&
                table.entries.size() == 1U &&
                table.entries[0U].match == 0 &&
                table.entries[0U].target_index == 2U,
            "tableswitch keys and targets are decoded once");
}

void test_lookupswitch_and_wide() {
    std::vector<phoneme::u8> lookup {0x03U, 0xABU, 0x11U, 0x22U};
    append_i32(lookup, 19); // default -> BCI 20
    append_i32(lookup, 1);  // pairs
    append_i32(lookup, 7);  // match
    append_i32(lookup, 19); // target -> BCI 20
    lookup.push_back(0x04U);
    lookup.push_back(0xACU);

    phoneme::classfile::CodeAttribute lookup_code {
        .max_stack = 1,
        .max_locals = 0,
        .bytecode = std::move(lookup),
    };
    auto decoded_lookup = phoneme::vm::decode_code(
        phoneme::vm::MethodId {3U}, lookup_code);
    require(decoded_lookup.has_value(), "decode lookupswitch");
    const auto& lookup_operand = decoded_lookup->operands[
        decoded_lookup->instructions[1U].operand_index];
    const auto& table = decoded_lookup->switches[
        lookup_operand.switch_index];
    require(table.lookup && table.entries.size() == 1U &&
                table.entries[0U].match == 7 &&
                table.entries[0U].target_bci == 20U,
            "lookupswitch preserves sorted match table");

    phoneme::classfile::CodeAttribute wide_code {
        .max_stack = 0,
        .max_locals = 259,
        .bytecode = {0xC4U, 0x84U, 0x01U, 0x02U,
                     0xFFU, 0xFEU, 0xB1U},
    };
    auto decoded_wide = phoneme::vm::decode_code(
        phoneme::vm::MethodId {4U}, wide_code);
    require(decoded_wide.has_value() &&
                decoded_wide->instructions.size() == 2U,
            "decode wide iinc as one instruction");
    const auto& wide = decoded_wide->operands[
        decoded_wide->instructions[0U].operand_index];
    require(wide.kind == phoneme::vm::DecodedOperandKind::wide_increment &&
                wide.modified_opcode == 0x84U &&
                wide.local_index == 0x0102U && wide.immediate == -2,
            "wide local and signed increment are predecoded");
}

void test_exception_mapping_and_rejection() {
    phoneme::classfile::CodeAttribute code {
        .max_stack = 1,
        .max_locals = 0,
        .bytecode = {0x01U, 0xBFU, 0xB1U},
        .exception_table = {
            phoneme::classfile::ExceptionHandler {
                .start_pc = 0U,
                .end_pc = 2U,
                .handler_pc = 2U,
                .catch_type = "java/lang/Throwable",
            },
        },
    };
    auto decoded = phoneme::vm::decode_code(
        phoneme::vm::MethodId {5U}, code);
    require(decoded.has_value() && decoded->exception_handlers.size() == 1U,
            "decode exception table");
    const auto& handler = decoded->exception_handlers[0U];
    require(handler.start_bci == 0U && handler.end_bci == 2U &&
                handler.handler_bci == 2U && handler.start_index == 0U &&
                handler.end_index == 2U && handler.handler_index == 2U,
            "exception ranges retain original BCIs and decoded indexes");

    phoneme::classfile::CodeAttribute malformed {
        .max_stack = 0,
        .max_locals = 0,
        .bytecode = {0xA7U, 0x00U, 0x02U, 0xB1U},
    };
    require(!phoneme::vm::decode_code(
                 phoneme::vm::MethodId {6U}, malformed)
                 .has_value(),
            "reject branch into operand bytes");

    phoneme::classfile::Method native_method {
        .access_flags = 0x0100U,
        .name = "nativeCall",
        .descriptor = "()V",
        .code = std::nullopt,
    };
    require(!phoneme::vm::decode_method(
                 phoneme::vm::MethodId {7U}, native_method)
                 .has_value(),
            "reject decoding method without Code attribute");
}

} // namespace

int main() {
    test_branch_mapping();
    test_tableswitch_padding_and_targets();
    test_lookupswitch_and_wide();
    test_exception_mapping_and_rejection();
    std::cout << "Decoded method tests passed\n";
    return 0;
}
