#pragma once

#include <span>

#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::classfile {

// Performs structural bytecode verification that does not require class loading.
// Type-state and StackMap verification are handled by the VM verifier layer.
[[nodiscard]] Status verify_code_structure(
    std::span<const u8> bytecode,
    std::span<const ExceptionHandler> exception_table,
    std::span<const StackMapFrame> stack_map_frames = {});

} // namespace phoneme::classfile
