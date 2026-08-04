#pragma once

#include <limits>
#include <vector>

#include "phoneme/base/Types.hpp"

namespace phoneme::vm {

inline constexpr u32 kInvalidDecodedIndex =
    std::numeric_limits<u32>::max();

enum class DecodedOpcode : u16 {
    jvm_first = 0x0000U,
    jvm_last = 0x00C9U,
    resolved_first = 0x0100U,
};

[[nodiscard]] constexpr DecodedOpcode decode_raw_opcode(u8 opcode) noexcept {
    return static_cast<DecodedOpcode>(static_cast<u16>(opcode));
}

[[nodiscard]] constexpr u8 raw_opcode(DecodedOpcode opcode) noexcept {
    return static_cast<u8>(static_cast<u16>(opcode));
}

enum class DecodedOperandKind : u8 {
    none,
    immediate,
    local_index,
    constant_pool_index,
    branch_target,
    increment,
    invokeinterface,
    invokedynamic,
    newarray_type,
    multianewarray,
    wide_local,
    wide_increment,
    switch_table,
};

struct DecodedOperand final {
    DecodedOperandKind kind {DecodedOperandKind::none};
    u16 constant_pool_index {0};
    u16 local_index {0};
    i32 immediate {0};
    u32 target_bci {kInvalidDecodedIndex};
    u32 target_index {kInvalidDecodedIndex};
    u32 switch_index {kInvalidDecodedIndex};
    u8 auxiliary {0};
    u8 modified_opcode {0};
};

struct DecodedSwitchEntry final {
    i32 match {0};
    u32 target_bci {kInvalidDecodedIndex};
    u32 target_index {kInvalidDecodedIndex};
};

struct DecodedSwitchTable final {
    bool lookup {false};
    i32 low {0};
    i32 high {-1};
    u32 default_target_bci {kInvalidDecodedIndex};
    u32 default_target_index {kInvalidDecodedIndex};
    std::vector<DecodedSwitchEntry> entries;
};

struct DecodedInstruction final {
    DecodedOpcode opcode {DecodedOpcode::jvm_first};
    u32 bytecode_pc {0};
    u32 next_index {kInvalidDecodedIndex};
    u32 operand_index {kInvalidDecodedIndex};
};

} // namespace phoneme::vm
