#pragma once

#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::vm {

// Verifies method descriptors, access/code invariants, control-flow stack depth,
// local value categories, instruction operand types, and parsed StackMap state.
// Class hierarchy assignability is intentionally deferred to linking.
[[nodiscard]] Status verify_class(const classfile::ClassFile& class_file);
[[nodiscard]] Status verify_method(const classfile::ClassFile& owner,
                                   const classfile::Method& method);

} // namespace phoneme::vm
