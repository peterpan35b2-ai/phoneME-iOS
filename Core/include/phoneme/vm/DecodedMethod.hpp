#pragma once

#include <string>
#include <vector>

#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/DecodedInstruction.hpp"
#include "phoneme/vm/MetadataId.hpp"

namespace phoneme::vm {

struct DecodedExceptionHandler final {
    u32 start_bci {0};
    u32 end_bci {0};
    u32 handler_bci {0};
    u32 start_index {kInvalidDecodedIndex};
    u32 end_index {kInvalidDecodedIndex};
    u32 handler_index {kInvalidDecodedIndex};
    std::string catch_type;
};

struct DecodedMethod final {
    MethodId method_id;
    u32 original_bytecode_size {0};
    std::vector<DecodedInstruction> instructions;
    std::vector<u32> bci_to_instruction;
    std::vector<DecodedOperand> operands;
    std::vector<DecodedSwitchTable> switches;
    std::vector<DecodedExceptionHandler> exception_handlers;

    [[nodiscard]] u32 instruction_index_for_bci(u32 bci) const noexcept;
};

[[nodiscard]] Result<DecodedMethod> decode_method(
    MethodId method_id,
    const classfile::Method& method);

[[nodiscard]] Result<DecodedMethod> decode_code(
    MethodId method_id,
    const classfile::CodeAttribute& code);

} // namespace phoneme::vm
