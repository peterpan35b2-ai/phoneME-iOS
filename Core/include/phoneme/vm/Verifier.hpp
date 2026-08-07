#pragma once

#include <vector>

#include "phoneme/classfile/ClassFile.hpp"

namespace phoneme::vm {

enum class VerifiedSlotKind : u8 {
    empty,
    continuation,
    int32,
    int64,
    float32,
    float64,
    reference,
    return_address,
};

struct VerifiedReferenceMap final {
    usize bytecode_pc {0};
    usize stack_slots {0};
    std::vector<usize> reference_slots;
    // Physical JVM slot kinds: max_locals entries followed by stack_slots
    // entries. Category-2 values occupy the value slot plus continuation.
    std::vector<VerifiedSlotKind> slot_kinds;
};

struct VerifiedMethodReferenceMaps final {
    usize max_locals {0};
    usize max_stack {0};
    std::vector<VerifiedReferenceMap> frames;
};

// Returns the verifier's exact physical JVM-slot reference maps at every
// reachable instruction. Local slots are indexed first, followed by operand
// stack slots. Uninitialized objects are roots as well because allocation may
// occur before their constructor has completed.
[[nodiscard]] Result<VerifiedMethodReferenceMaps> verified_reference_maps(
    const classfile::ClassFile& owner,
    const classfile::Method& method);

// Verifies method descriptors, access/code invariants, control-flow stack depth,
// local value categories, instruction operand types, and parsed StackMap state.
// Class hierarchy assignability is intentionally deferred to linking.
[[nodiscard]] Status verify_class(const classfile::ClassFile& class_file);
[[nodiscard]] Status verify_method(const classfile::ClassFile& owner,
                                   const classfile::Method& method);

} // namespace phoneme::vm
