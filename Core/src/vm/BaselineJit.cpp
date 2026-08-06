#include "phoneme/vm/BaselineJit.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <dlfcn.h>
#include <libkern/OSCacheControl.h>
#endif

#if defined(__arm64e__) && __has_include(<ptrauth.h>)
#include <ptrauth.h>
#endif

#if defined(__aarch64__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace phoneme::vm {
namespace {

constexpr u32 kDefaultHotThreshold = 32U;
constexpr u32 kMaximumHotThreshold = 10'000U;
constexpr u64 kDefaultCodeCacheBytes = 16U * 1024U * 1024U;
constexpr u64 kMinimumCodeCacheBytes = 1U * 1024U * 1024U;
constexpr u64 kMaximumCodeCacheBytes = 256U * 1024U * 1024U;
constexpr u32 kUnavailableProbeInterval = 256U;
constexpr usize kMaximumSwitchCases = 512U;
constexpr usize kMaximumNativeInstructions = 256U * 1024U;

[[nodiscard]] u32 configured_hot_threshold() noexcept {
    const char* value = std::getenv("PHONEME_JIT_HOT_THRESHOLD");
    if (value == nullptr || *value == '\0') return kDefaultHotThreshold;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0UL) {
        return kDefaultHotThreshold;
    }
    return static_cast<u32>(std::min<unsigned long>(
        parsed, static_cast<unsigned long>(kMaximumHotThreshold)));
}

[[nodiscard]] u64 configured_code_cache_bytes() noexcept {
    const char* value = std::getenv("PHONEME_JIT_CODE_CACHE_MB");
    if (value == nullptr || *value == '\0') return kDefaultCodeCacheBytes;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0UL) {
        return kDefaultCodeCacheBytes;
    }
    const u64 bytes = static_cast<u64>(parsed) * 1024U * 1024U;
    return std::clamp(bytes, kMinimumCodeCacheBytes, kMaximumCodeCacheBytes);
}

[[nodiscard]] bool integer_like(JavaTypeKind kind) noexcept {
    switch (kind) {
    case JavaTypeKind::boolean:
    case JavaTypeKind::byte:
    case JavaTypeKind::character:
    case JavaTypeKind::short_integer:
    case JavaTypeKind::integer:
        return true;
    default:
        return false;
    }
}

#if defined(__aarch64__)

using JitFunction = u64 (*)(const u64* arguments,
                            u32 instruction_budget,
                            u64* result_bits);

constexpr u32 kScratchLeft = 9U;
constexpr u32 kScratchRight = 10U;
constexpr u32 kBudgetRemaining = 11U;
constexpr u32 kBudgetInitial = 12U;
constexpr u32 kScratchThird = 13U;
constexpr u32 kScratchFourth = 14U;
constexpr u32 kResultPointer = 15U;
constexpr u32 kStackPointer = 31U;
constexpr u64 kDeoptMarker = 1ULL << 63U;
constexpr u64 kBudgetExhaustedMarker = 1ULL << 62U;
constexpr u32 kMaximumPackedBudget = 0x3FFF'FFFFU;

enum class Arm64Condition : u32 {
    equal = 0x0U,
    not_equal = 0x1U,
    unsigned_lower = 0x3U,
    greater_equal = 0xAU,
    less_than = 0xBU,
    greater_than = 0xCU,
    less_equal = 0xDU,
};

struct ExecutableBlock final {
    void* memory {nullptr};
    usize mapped_size {0};
    JitFunction function {nullptr};

    ExecutableBlock() = default;
    ExecutableBlock(void* initial_memory,
                    usize initial_size,
                    JitFunction initial_function) noexcept
        : memory(initial_memory),
          mapped_size(initial_size),
          function(initial_function) {}

    ExecutableBlock(const ExecutableBlock&) = delete;
    ExecutableBlock& operator=(const ExecutableBlock&) = delete;

    ExecutableBlock(ExecutableBlock&& other) noexcept
        : memory(std::exchange(other.memory, nullptr)),
          mapped_size(std::exchange(other.mapped_size, 0U)),
          function(std::exchange(other.function, nullptr)) {}

    ExecutableBlock& operator=(ExecutableBlock&& other) noexcept {
        if (this == &other) return *this;
        reset();
        memory = std::exchange(other.memory, nullptr);
        mapped_size = std::exchange(other.mapped_size, 0U);
        function = std::exchange(other.function, nullptr);
        return *this;
    }

    ~ExecutableBlock() { reset(); }

    void reset() noexcept {
        if (memory != nullptr && mapped_size != 0U) {
            (void)::munmap(memory, mapped_size);
        }
        memory = nullptr;
        mapped_size = 0U;
        function = nullptr;
    }
};

class Arm64Emitter final {
public:
    [[nodiscard]] usize position() const noexcept { return code_.size(); }
    [[nodiscard]] const std::vector<u32>& code() const noexcept { return code_; }

    void emit(u32 instruction) { code_.push_back(instruction); }

    void sub_sp(u32 bytes) {
        emit(0xD10003FFU | ((bytes & 0xFFFU) << 10U));
    }

    void add_sp(u32 bytes) {
        emit(0x910003FFU | ((bytes & 0xFFFU) << 10U));
    }

    void load_w(u32 target, u32 base, u32 byte_offset) {
        emit(0xB9400000U |
             (((byte_offset / 4U) & 0xFFFU) << 10U) |
             ((base & 0x1FU) << 5U) |
             (target & 0x1FU));
    }

    void store_w(u32 source, u32 base, u32 byte_offset) {
        emit(0xB9000000U |
             (((byte_offset / 4U) & 0xFFFU) << 10U) |
             ((base & 0x1FU) << 5U) |
             (source & 0x1FU));
    }

    void load_x(u32 target, u32 base, u32 byte_offset) {
        emit(0xF9400000U |
             (((byte_offset / 8U) & 0xFFFU) << 10U) |
             ((base & 0x1FU) << 5U) |
             (target & 0x1FU));
    }

    void store_x(u32 source, u32 base, u32 byte_offset) {
        emit(0xF9000000U |
             (((byte_offset / 8U) & 0xFFFU) << 10U) |
             ((base & 0x1FU) << 5U) |
             (source & 0x1FU));
    }

    void move_imm32(u32 target, u32 value) {
        emit(0x52800000U | ((value & 0xFFFFU) << 5U) |
             (target & 0x1FU));
        if ((value >> 16U) != 0U) {
            emit(0x72A00000U | (((value >> 16U) & 0xFFFFU) << 5U) |
                 (target & 0x1FU));
        }
    }

    void move_w(u32 destination, u32 source) {
        emit(0x2A0003E0U | ((source & 0x1FU) << 16U) |
             (destination & 0x1FU));
    }

    void move_x(u32 destination, u32 source) {
        emit(0xAA0003E0U | ((source & 0x1FU) << 16U) |
             (destination & 0x1FU));
    }

    void add_w(u32 destination, u32 left, u32 right) {
        emit(0x0B000000U | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void sub_w(u32 destination, u32 left, u32 right) {
        emit(0x4B000000U | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void sub_imm_w(u32 destination, u32 source, u32 immediate) {
        emit(0x51000000U | ((immediate & 0xFFFU) << 10U) |
             ((source & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void multiply_w(u32 destination, u32 left, u32 right) {
        emit(0x1B007C00U | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void divide_signed_w(u32 destination, u32 left, u32 right) {
        emit(0x1AC00C00U | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void multiply_subtract_w(u32 destination,
                             u32 multiplier,
                             u32 multiplicand,
                             u32 minuend) {
        emit(0x1B008000U | ((multiplicand & 0x1FU) << 16U) |
             ((minuend & 0x1FU) << 10U) |
             ((multiplier & 0x1FU) << 5U) |
             (destination & 0x1FU));
    }

    void and_w(u32 destination, u32 left, u32 right) {
        emit(0x0A000000U | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void or_w(u32 destination, u32 left, u32 right) {
        emit(0x2A000000U | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void xor_w(u32 destination, u32 left, u32 right) {
        emit(0x4A000000U | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void negate_w(u32 destination, u32 source) {
        emit(0x4B000000U | ((source & 0x1FU) << 16U) |
             (31U << 5U) | (destination & 0x1FU));
    }

    void shift_left_w(u32 destination, u32 value, u32 shift) {
        emit(0x1AC02000U | ((shift & 0x1FU) << 16U) |
             ((value & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void shift_right_logical_w(u32 destination, u32 value, u32 shift) {
        emit(0x1AC02400U | ((shift & 0x1FU) << 16U) |
             ((value & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void shift_right_arithmetic_w(u32 destination, u32 value, u32 shift) {
        emit(0x1AC02800U | ((shift & 0x1FU) << 16U) |
             ((value & 0x1FU) << 5U) | (destination & 0x1FU));
    }

    void sign_extend_byte_w(u32 destination, u32 source) {
        emit(0x13001C00U | ((source & 0x1FU) << 5U) |
             (destination & 0x1FU));
    }

    void sign_extend_half_w(u32 destination, u32 source) {
        emit(0x13003C00U | ((source & 0x1FU) << 5U) |
             (destination & 0x1FU));
    }

    void zero_extend_half_w(u32 destination, u32 source) {
        emit(0x53003C00U | ((source & 0x1FU) << 5U) |
             (destination & 0x1FU));
    }

    void compare_zero_w(u32 value) {
        emit(0x7100001FU | ((value & 0x1FU) << 5U));
    }

    void compare_imm_w(u32 value, u32 immediate) {
        emit(0x7100001FU | ((immediate & 0xFFFU) << 10U) |
             ((value & 0x1FU) << 5U));
    }

    void compare_w(u32 left, u32 right) {
        emit(0x6B00001FU | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U));
    }

    void compare_zero_x(u32 value) {
        emit(0xF100001FU | ((value & 0x1FU) << 5U));
    }

    void compare_x(u32 left, u32 right) {
        emit(0xEB00001FU | ((right & 0x1FU) << 16U) |
             ((left & 0x1FU) << 5U));
    }

    [[nodiscard]] usize emit_branch_placeholder() {
        const usize index = position();
        emit(0x14000000U);
        return index;
    }

    [[nodiscard]] usize emit_conditional_branch_placeholder(
        Arm64Condition condition) {
        const usize index = position();
        emit(0x54000000U | static_cast<u32>(condition));
        return index;
    }

    [[nodiscard]] usize emit_cbz_w_placeholder(u32 value) {
        const usize index = position();
        emit(0x34000000U | (value & 0x1FU));
        return index;
    }

    [[nodiscard]] bool patch_branch(usize index, usize target) {
        if (index >= code_.size()) return false;
        const i64 delta = static_cast<i64>(target) - static_cast<i64>(index);
        if (delta < -(1LL << 25U) || delta >= (1LL << 25U)) return false;
        code_[index] = 0x14000000U |
            (static_cast<u32>(delta) & 0x03FF'FFFFU);
        return true;
    }

    [[nodiscard]] bool patch_conditional_branch(usize index,
                                                usize target,
                                                Arm64Condition condition) {
        if (index >= code_.size()) return false;
        const i64 delta = static_cast<i64>(target) - static_cast<i64>(index);
        if (delta < -(1LL << 18U) || delta >= (1LL << 18U)) return false;
        code_[index] = 0x54000000U |
            ((static_cast<u32>(delta) & 0x7FFFFU) << 5U) |
            static_cast<u32>(condition);
        return true;
    }

    [[nodiscard]] bool patch_cbz_w(usize index, usize target, u32 value) {
        if (index >= code_.size()) return false;
        const i64 delta = static_cast<i64>(target) - static_cast<i64>(index);
        if (delta < -(1LL << 18U) || delta >= (1LL << 18U)) return false;
        code_[index] = 0x34000000U |
            ((static_cast<u32>(delta) & 0x7FFFFU) << 5U) |
            (value & 0x1FU);
        return true;
    }

    void zero_extend_w_to_x(u32 destination, u32 source) {
        emit(0xD3407C00U | ((source & 0x1FU) << 5U) |
             (destination & 0x1FU));
    }

    void or_x_shifted(u32 destination,
                      u32 left,
                      u32 right,
                      u32 shift) {
        emit(0xAA000000U | ((right & 0x1FU) << 16U) |
             ((shift & 0x3FU) << 10U) |
             ((left & 0x1FU) << 5U) |
             (destination & 0x1FU));
    }

    void move_deopt_marker_x0() { emit(0xD2F00000U); }
    void move_budget_deopt_marker_x0() { emit(0xD2F80000U); }
    void ret() { emit(0xD65F03C0U); }

private:
    std::vector<u32> code_;
};

struct CompiledMethod final {
    ExecutableBlock block;
    JavaTypeKind return_kind {JavaTypeKind::void_type};
    bool returns_value {false};
    bool optimized {false};
    bool contains_loop {false};
    u64 propagated_constants {0};
    u64 folded_operations {0};
    u64 folded_branches {0};
    u64 strength_reductions {0};
    u64 budget_checks_elided {0};
};

enum class CompileDisposition : u8 {
    success,
    unsupported,
    executable_memory_unavailable,
};

struct CompileAttempt final {
    CompileDisposition disposition {CompileDisposition::unsupported};
    std::optional<CompiledMethod> method;
};

struct SwitchTarget final {
    i32 key {0};
    usize target_pc {0};
};

struct DecodedInstruction final {
    usize pc {0};
    usize next_pc {0};
    u8 opcode {0};
    u32 local_index {0};
    i32 immediate {0};
    std::optional<usize> branch_target;
    std::optional<usize> default_target;
    std::vector<SwitchTarget> switch_targets;
};

struct BranchPatch final {
    enum class Kind : u8 { unconditional, conditional };
    Kind kind {Kind::unconditional};
    usize native_index {0};
    usize target_pc {0};
    Arm64Condition condition {Arm64Condition::equal};
};

struct DeoptPatch final {
    usize native_index {0};
    u32 register_index {0};
};

[[nodiscard]] usize system_page_size() noexcept {
    const long page_size = ::sysconf(_SC_PAGESIZE);
    return page_size > 0L ? static_cast<usize>(page_size) : 4096U;
}

#if defined(__APPLE__)
void set_thread_jit_write_protection(bool enabled) noexcept {
    using SupportedFunction = int (*)(void);
    using ProtectFunction = void (*)(int);
    static const auto supported = reinterpret_cast<SupportedFunction>(
        ::dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_supported_np"));
    static const auto protect = reinterpret_cast<ProtectFunction>(
        ::dlsym(RTLD_DEFAULT, "pthread_jit_write_protect_np"));
    if (supported != nullptr && protect != nullptr && supported() != 0) {
        protect(enabled ? 1 : 0);
    }
}
#else
void set_thread_jit_write_protection(bool) noexcept {}
#endif

void invalidate_instruction_cache(void* memory, usize size) noexcept {
#if defined(__APPLE__)
    ::sys_icache_invalidate(memory, size);
#else
    auto* begin = static_cast<char*>(memory);
    __builtin___clear_cache(begin, begin + size);
#endif
}

[[nodiscard]] JitFunction function_at(void* memory) noexcept {
    JitFunction function = nullptr;
#if defined(__arm64e__) && __has_include(<ptrauth.h>)
    void* signed_pointer = ptrauth_sign_unauthenticated(
        memory,
        ptrauth_key_function_pointer,
        0);
    static_assert(sizeof(function) == sizeof(signed_pointer));
    std::memcpy(&function, &signed_pointer, sizeof(function));
#else
    static_assert(sizeof(function) == sizeof(memory));
    std::memcpy(&function, &memory, sizeof(function));
#endif
    return function;
}

[[nodiscard]] std::optional<ExecutableBlock> finalize_with_mapping(
    const std::vector<u32>& code,
    usize mapped_size,
    int flags,
    int initial_protection,
    bool uses_map_jit) noexcept {
    void* memory = ::mmap(nullptr,
                          mapped_size,
                          initial_protection,
                          flags,
                          -1,
                          0);
    if (memory == MAP_FAILED) return std::nullopt;

    const usize byte_count = code.size() * sizeof(u32);
    if (uses_map_jit) set_thread_jit_write_protection(false);
    std::memcpy(memory, code.data(), byte_count);
    invalidate_instruction_cache(memory, byte_count);
    if (uses_map_jit) set_thread_jit_write_protection(true);

    if ((initial_protection & PROT_EXEC) == 0) {
        if (::mprotect(memory, mapped_size, PROT_READ | PROT_EXEC) != 0) {
            (void)::munmap(memory, mapped_size);
            return std::nullopt;
        }
    } else {
        // Prefer W^X after finalization. Some debugger-enabled iOS versions
        // permit the initial RWX mapping but reject the later protection
        // transition; in that case the already executable mapping remains a
        // valid last-resort JIT path.
        (void)::mprotect(memory, mapped_size, PROT_READ | PROT_EXEC);
    }

    return ExecutableBlock(memory, mapped_size, function_at(memory));
}

[[nodiscard]] std::optional<ExecutableBlock> finalize_code(
    const std::vector<u32>& code) noexcept {
    if (code.empty() || code.size() > kMaximumNativeInstructions) {
        return std::nullopt;
    }
    const usize byte_count = code.size() * sizeof(u32);
    const usize page_size = system_page_size();
    if (byte_count > std::numeric_limits<usize>::max() - (page_size - 1U)) {
        return std::nullopt;
    }
    const usize mapped_size =
        ((byte_count + page_size - 1U) / page_size) * page_size;
    const int base_flags = MAP_PRIVATE | MAP_ANON;

    if (auto block = finalize_with_mapping(
            code,
            mapped_size,
            base_flags,
            PROT_READ | PROT_WRITE,
            false)) {
        return block;
    }

#if defined(__APPLE__) && defined(MAP_JIT)
    if (auto block = finalize_with_mapping(
            code,
            mapped_size,
            base_flags | MAP_JIT,
            PROT_READ | PROT_WRITE | PROT_EXEC,
            true)) {
        return block;
    }
    if (auto block = finalize_with_mapping(
            code,
            mapped_size,
            base_flags | MAP_JIT,
            PROT_READ | PROT_WRITE,
            true)) {
        return block;
    }
#endif

    return finalize_with_mapping(
        code,
        mapped_size,
        base_flags,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        false);
}

[[nodiscard]] bool read_u8(const std::vector<u8>& code,
                           usize& pc,
                           u8& value) noexcept {
    if (pc >= code.size()) return false;
    value = code[pc++];
    return true;
}

[[nodiscard]] bool read_u16(const std::vector<u8>& code,
                            usize& pc,
                            u16& value) noexcept {
    u8 high = 0U;
    u8 low = 0U;
    if (!read_u8(code, pc, high) || !read_u8(code, pc, low)) return false;
    value = static_cast<u16>((static_cast<u16>(high) << 8U) |
                             static_cast<u16>(low));
    return true;
}

[[nodiscard]] bool read_i8(const std::vector<u8>& code,
                           usize& pc,
                           i32& value) noexcept {
    u8 raw = 0U;
    if (!read_u8(code, pc, raw)) return false;
    value = static_cast<i32>(static_cast<i8>(raw));
    return true;
}

[[nodiscard]] bool read_i16(const std::vector<u8>& code,
                            usize& pc,
                            i32& value) noexcept {
    u16 raw = 0U;
    if (!read_u16(code, pc, raw)) return false;
    value = static_cast<i32>(static_cast<i16>(raw));
    return true;
}

[[nodiscard]] bool read_i32(const std::vector<u8>& code,
                            usize& pc,
                            i32& value) noexcept {
    u8 b0 = 0U;
    u8 b1 = 0U;
    u8 b2 = 0U;
    u8 b3 = 0U;
    if (!read_u8(code, pc, b0) || !read_u8(code, pc, b1) ||
        !read_u8(code, pc, b2) || !read_u8(code, pc, b3)) {
        return false;
    }
    const u32 raw = (static_cast<u32>(b0) << 24U) |
                    (static_cast<u32>(b1) << 16U) |
                    (static_cast<u32>(b2) << 8U) |
                    static_cast<u32>(b3);
    value = static_cast<i32>(raw);
    return true;
}

[[nodiscard]] std::optional<usize> branch_target(usize pc,
                                                 i32 offset,
                                                 usize code_size) noexcept {
    const i64 target = static_cast<i64>(pc) + static_cast<i64>(offset);
    if (target < 0 || target >= static_cast<i64>(code_size)) {
        return std::nullopt;
    }
    return static_cast<usize>(target);
}

[[nodiscard]] bool is_unary_branch(u8 opcode) noexcept {
    return opcode >= 0x99U && opcode <= 0x9EU;
}

[[nodiscard]] bool is_binary_branch(u8 opcode) noexcept {
    return opcode >= 0x9FU && opcode <= 0xA4U;
}

[[nodiscard]] bool is_reference_branch(u8 opcode) noexcept {
    return opcode == 0xA5U || opcode == 0xA6U ||
           opcode == 0xC6U || opcode == 0xC7U;
}

[[nodiscard]] bool is_conditional_branch(u8 opcode) noexcept {
    return is_unary_branch(opcode) || is_binary_branch(opcode) ||
           is_reference_branch(opcode);
}

[[nodiscard]] bool is_goto(u8 opcode) noexcept {
    return opcode == 0xA7U || opcode == 0xC8U;
}

[[nodiscard]] std::optional<Arm64Condition> branch_condition(
    u8 opcode) noexcept {
    switch (opcode) {
    case 0x99U:
    case 0x9FU:
    case 0xA5U:
    case 0xC6U:
        return Arm64Condition::equal;
    case 0x9AU:
    case 0xA0U:
    case 0xA6U:
    case 0xC7U:
        return Arm64Condition::not_equal;
    case 0x9BU:
    case 0xA1U:
        return Arm64Condition::less_than;
    case 0x9CU:
    case 0xA2U:
        return Arm64Condition::greater_equal;
    case 0x9DU:
    case 0xA3U:
        return Arm64Condition::greater_than;
    case 0x9EU:
    case 0xA4U:
        return Arm64Condition::less_equal;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<std::vector<DecodedInstruction>> decode_method(
    const classfile::ClassFile& owner,
    const classfile::Method& method) {
    if (!method.code.has_value()) return std::nullopt;
    const std::vector<u8>& code = method.code->bytecode;
    if (code.empty()) return std::nullopt;

    std::vector<DecodedInstruction> instructions;
    usize pc = 0U;
    while (pc < code.size()) {
        DecodedInstruction instruction;
        instruction.pc = pc;
        if (!read_u8(code, pc, instruction.opcode)) return std::nullopt;

        switch (instruction.opcode) {
        case 0x00U:
        case 0x01U:
        case 0x02U:
        case 0x03U:
        case 0x04U:
        case 0x05U:
        case 0x06U:
        case 0x07U:
        case 0x08U:
        case 0x1AU:
        case 0x1BU:
        case 0x1CU:
        case 0x1DU:
        case 0x2AU:
        case 0x2BU:
        case 0x2CU:
        case 0x2DU:
        case 0x3BU:
        case 0x3CU:
        case 0x3DU:
        case 0x3EU:
        case 0x4BU:
        case 0x4CU:
        case 0x4DU:
        case 0x4EU:
        case 0x57U:
        case 0x58U:
        case 0x59U:
        case 0x5AU:
        case 0x5BU:
        case 0x5CU:
        case 0x5DU:
        case 0x5EU:
        case 0x5FU:
        case 0x60U:
        case 0x64U:
        case 0x68U:
        case 0x6CU:
        case 0x70U:
        case 0x74U:
        case 0x78U:
        case 0x7AU:
        case 0x7CU:
        case 0x7EU:
        case 0x80U:
        case 0x82U:
        case 0x91U:
        case 0x92U:
        case 0x93U:
        case 0xACU:
        case 0xB0U:
        case 0xB1U:
            break;

        case 0x10U:
            if (!read_i8(code, pc, instruction.immediate)) {
                return std::nullopt;
            }
            break;

        case 0x11U:
            if (!read_i16(code, pc, instruction.immediate)) {
                return std::nullopt;
            }
            break;

        case 0x12U: {
            u8 index = 0U;
            if (!read_u8(code, pc, index)) return std::nullopt;
            instruction.local_index = index;
            auto constant = owner.constant(static_cast<u16>(index));
            if (!constant || (*constant)->kind != classfile::ConstantKind::integer) {
                return std::nullopt;
            }
            instruction.immediate = static_cast<i32>(
                static_cast<u32>((*constant)->bits));
            break;
        }

        case 0x13U: {
            u16 index = 0U;
            if (!read_u16(code, pc, index)) return std::nullopt;
            instruction.local_index = index;
            auto constant = owner.constant(index);
            if (!constant || (*constant)->kind != classfile::ConstantKind::integer) {
                return std::nullopt;
            }
            instruction.immediate = static_cast<i32>(
                static_cast<u32>((*constant)->bits));
            break;
        }

        case 0x15U:
        case 0x19U:
        case 0x36U:
        case 0x3AU: {
            u8 index = 0U;
            if (!read_u8(code, pc, index)) return std::nullopt;
            instruction.local_index = index;
            break;
        }

        case 0x84U: {
            u8 index = 0U;
            if (!read_u8(code, pc, index) ||
                !read_i8(code, pc, instruction.immediate)) {
                return std::nullopt;
            }
            instruction.local_index = index;
            break;
        }

        case 0x99U:
        case 0x9AU:
        case 0x9BU:
        case 0x9CU:
        case 0x9DU:
        case 0x9EU:
        case 0x9FU:
        case 0xA0U:
        case 0xA1U:
        case 0xA2U:
        case 0xA3U:
        case 0xA4U:
        case 0xA5U:
        case 0xA6U:
        case 0xA7U:
        case 0xC6U:
        case 0xC7U: {
            i32 offset = 0;
            if (!read_i16(code, pc, offset)) return std::nullopt;
            instruction.branch_target = branch_target(
                instruction.pc, offset, code.size());
            if (!instruction.branch_target.has_value()) return std::nullopt;
            break;
        }

        case 0xAAU:
        case 0xABU: {
            while ((pc & 3U) != 0U) {
                u8 padding = 0U;
                if (!read_u8(code, pc, padding) || padding != 0U) {
                    return std::nullopt;
                }
            }
            i32 default_offset = 0;
            if (!read_i32(code, pc, default_offset)) return std::nullopt;
            instruction.default_target = branch_target(
                instruction.pc, default_offset, code.size());
            if (!instruction.default_target.has_value()) return std::nullopt;

            if (instruction.opcode == 0xAAU) {
                i32 low = 0;
                i32 high = 0;
                if (!read_i32(code, pc, low) || !read_i32(code, pc, high)) {
                    return std::nullopt;
                }
                const i64 count = static_cast<i64>(high) -
                                  static_cast<i64>(low) + 1;
                if (count <= 0 ||
                    count > static_cast<i64>(kMaximumSwitchCases)) {
                    return std::nullopt;
                }
                instruction.switch_targets.reserve(static_cast<usize>(count));
                for (i64 index = 0; index < count; ++index) {
                    i32 offset = 0;
                    if (!read_i32(code, pc, offset)) return std::nullopt;
                    auto target = branch_target(
                        instruction.pc, offset, code.size());
                    if (!target.has_value()) return std::nullopt;
                    instruction.switch_targets.push_back(SwitchTarget {
                        .key = static_cast<i32>(static_cast<i64>(low) + index),
                        .target_pc = *target,
                    });
                }
            } else {
                i32 pair_count = 0;
                if (!read_i32(code, pc, pair_count) || pair_count < 0 ||
                    pair_count > static_cast<i32>(kMaximumSwitchCases)) {
                    return std::nullopt;
                }
                instruction.switch_targets.reserve(
                    static_cast<usize>(pair_count));
                i32 previous_key = std::numeric_limits<i32>::min();
                for (i32 index = 0; index < pair_count; ++index) {
                    i32 key = 0;
                    i32 offset = 0;
                    if (!read_i32(code, pc, key) ||
                        !read_i32(code, pc, offset) ||
                        (index != 0 && key <= previous_key)) {
                        return std::nullopt;
                    }
                    auto target = branch_target(
                        instruction.pc, offset, code.size());
                    if (!target.has_value()) return std::nullopt;
                    instruction.switch_targets.push_back(SwitchTarget {
                        .key = key,
                        .target_pc = *target,
                    });
                    previous_key = key;
                }
            }
            break;
        }

        case 0xC4U: {
            u8 wide_opcode = 0U;
            u16 index = 0U;
            if (!read_u8(code, pc, wide_opcode) ||
                !read_u16(code, pc, index)) {
                return std::nullopt;
            }
            if (wide_opcode != 0x15U && wide_opcode != 0x19U &&
                wide_opcode != 0x36U && wide_opcode != 0x3AU &&
                wide_opcode != 0x84U) {
                return std::nullopt;
            }
            instruction.opcode = wide_opcode;
            instruction.local_index = index;
            if (wide_opcode == 0x84U &&
                !read_i16(code, pc, instruction.immediate)) {
                return std::nullopt;
            }
            break;
        }

        case 0xC8U: {
            i32 offset = 0;
            if (!read_i32(code, pc, offset)) return std::nullopt;
            instruction.branch_target = branch_target(
                instruction.pc, offset, code.size());
            if (!instruction.branch_target.has_value()) return std::nullopt;
            break;
        }

        default:
            return std::nullopt;
        }

        instruction.next_pc = pc;
        instructions.push_back(std::move(instruction));
    }
    return instructions;
}

struct StackShape final {
    u32 pop {0};
    u32 push {0};
};

[[nodiscard]] std::optional<StackShape> stack_shape(
    const DecodedInstruction& instruction,
    JavaTypeKind return_kind) noexcept {
    switch (instruction.opcode) {
    case 0x00U:
    case 0x84U:
    case 0xA7U:
    case 0xC8U:
        return StackShape {};
    case 0x01U:
    case 0x02U:
    case 0x03U:
    case 0x04U:
    case 0x05U:
    case 0x06U:
    case 0x07U:
    case 0x08U:
    case 0x10U:
    case 0x11U:
    case 0x12U:
    case 0x13U:
    case 0x15U:
    case 0x19U:
    case 0x1AU:
    case 0x1BU:
    case 0x1CU:
    case 0x1DU:
    case 0x2AU:
    case 0x2BU:
    case 0x2CU:
    case 0x2DU:
        return StackShape {.pop = 0U, .push = 1U};
    case 0x36U:
    case 0x3AU:
    case 0x3BU:
    case 0x3CU:
    case 0x3DU:
    case 0x3EU:
    case 0x4BU:
    case 0x4CU:
    case 0x4DU:
    case 0x4EU:
    case 0x57U:
        return StackShape {.pop = 1U, .push = 0U};
    case 0x58U:
        return StackShape {.pop = 2U, .push = 0U};
    case 0x59U:
        return StackShape {.pop = 1U, .push = 2U};
    case 0x5AU:
        return StackShape {.pop = 2U, .push = 3U};
    case 0x5BU:
        return StackShape {.pop = 3U, .push = 4U};
    case 0x5CU:
        return StackShape {.pop = 2U, .push = 4U};
    case 0x5DU:
        return StackShape {.pop = 3U, .push = 5U};
    case 0x5EU:
        return StackShape {.pop = 4U, .push = 6U};
    case 0x5FU:
        return StackShape {.pop = 2U, .push = 2U};
    case 0x60U:
    case 0x64U:
    case 0x68U:
    case 0x6CU:
    case 0x70U:
    case 0x78U:
    case 0x7AU:
    case 0x7CU:
    case 0x7EU:
    case 0x80U:
    case 0x82U:
        return StackShape {.pop = 2U, .push = 1U};
    case 0x74U:
    case 0x91U:
    case 0x92U:
    case 0x93U:
        return StackShape {.pop = 1U, .push = 1U};
    case 0x99U:
    case 0x9AU:
    case 0x9BU:
    case 0x9CU:
    case 0x9DU:
    case 0x9EU:
    case 0xC6U:
    case 0xC7U:
    case 0xAAU:
    case 0xABU:
        return StackShape {.pop = 1U, .push = 0U};
    case 0x9FU:
    case 0xA0U:
    case 0xA1U:
    case 0xA2U:
    case 0xA3U:
    case 0xA4U:
    case 0xA5U:
    case 0xA6U:
        return StackShape {.pop = 2U, .push = 0U};
    case 0xACU:
        if (!integer_like(return_kind)) return std::nullopt;
        return StackShape {.pop = 1U, .push = 0U};
    case 0xB0U:
        if (return_kind != JavaTypeKind::reference &&
            return_kind != JavaTypeKind::array) {
            return std::nullopt;
        }
        return StackShape {.pop = 1U, .push = 0U};
    case 0xB1U:
        if (return_kind != JavaTypeKind::void_type) return std::nullopt;
        return StackShape {};
    default:
        return std::nullopt;
    }
}

[[nodiscard]] bool uses_local_index(u8 opcode) noexcept {
    return opcode == 0x15U || opcode == 0x19U ||
           opcode == 0x36U || opcode == 0x3AU || opcode == 0x84U;
}

[[nodiscard]] std::optional<std::vector<std::optional<u32>>>
compute_stack_depths(const std::vector<DecodedInstruction>& instructions,
                     u32 local_slots,
                     u32 stack_slots,
                     JavaTypeKind return_kind) {
    if (instructions.empty() || instructions.front().pc != 0U) {
        return std::nullopt;
    }

    std::unordered_map<usize, usize> index_by_pc;
    index_by_pc.reserve(instructions.size());
    for (usize index = 0; index < instructions.size(); ++index) {
        if (!index_by_pc.emplace(instructions[index].pc, index).second) {
            return std::nullopt;
        }
        if (uses_local_index(instructions[index].opcode) &&
            instructions[index].local_index >= local_slots) {
            return std::nullopt;
        }
        if (instructions[index].opcode >= 0x1AU &&
            instructions[index].opcode <= 0x1DU &&
            static_cast<u32>(instructions[index].opcode - 0x1AU) >=
                local_slots) {
            return std::nullopt;
        }
        if (instructions[index].opcode >= 0x3BU &&
            instructions[index].opcode <= 0x3EU &&
            static_cast<u32>(instructions[index].opcode - 0x3BU) >=
                local_slots) {
            return std::nullopt;
        }
        if (instructions[index].opcode >= 0x2AU &&
            instructions[index].opcode <= 0x2DU &&
            static_cast<u32>(instructions[index].opcode - 0x2AU) >=
                local_slots) {
            return std::nullopt;
        }
        if (instructions[index].opcode >= 0x4BU &&
            instructions[index].opcode <= 0x4EU &&
            static_cast<u32>(instructions[index].opcode - 0x4BU) >=
                local_slots) {
            return std::nullopt;
        }
    }

    const auto resolve = [&](usize target_pc) -> std::optional<usize> {
        const auto found = index_by_pc.find(target_pc);
        if (found == index_by_pc.end()) return std::nullopt;
        return found->second;
    };

    std::vector<std::optional<u32>> depths(instructions.size());
    std::deque<usize> worklist;
    depths[0] = 0U;
    worklist.push_back(0U);

    const auto merge_depth = [&](usize successor,
                                 u32 depth,
                                 auto& queue,
                                 auto& values) -> bool {
        if (!values[successor].has_value()) {
            values[successor] = depth;
            queue.push_back(successor);
            return true;
        }
        return *values[successor] == depth;
    };

    while (!worklist.empty()) {
        const usize index = worklist.front();
        worklist.pop_front();
        const DecodedInstruction& instruction = instructions[index];
        const auto shape = stack_shape(instruction, return_kind);
        if (!shape.has_value() || *depths[index] < shape->pop) {
            return std::nullopt;
        }
        const u32 output_depth = *depths[index] - shape->pop + shape->push;
        if (output_depth > stack_slots) return std::nullopt;

        if (instruction.opcode == 0xACU || instruction.opcode == 0xB0U ||
            instruction.opcode == 0xB1U) {
            if (output_depth != 0U) return std::nullopt;
            continue;
        }

        if (is_goto(instruction.opcode)) {
            if (!instruction.branch_target.has_value()) return std::nullopt;
            const auto successor = resolve(*instruction.branch_target);
            if (!successor.has_value() ||
                !merge_depth(*successor, output_depth, worklist, depths)) {
                return std::nullopt;
            }
            continue;
        }

        if (is_conditional_branch(instruction.opcode)) {
            if (!instruction.branch_target.has_value()) return std::nullopt;
            const auto branch = resolve(*instruction.branch_target);
            const auto fallthrough = resolve(instruction.next_pc);
            if (!branch.has_value() || !fallthrough.has_value() ||
                !merge_depth(*branch, output_depth, worklist, depths) ||
                !merge_depth(*fallthrough, output_depth, worklist, depths)) {
                return std::nullopt;
            }
            continue;
        }

        if (instruction.opcode == 0xAAU || instruction.opcode == 0xABU) {
            if (!instruction.default_target.has_value()) return std::nullopt;
            const auto default_successor = resolve(*instruction.default_target);
            if (!default_successor.has_value() ||
                !merge_depth(*default_successor,
                             output_depth,
                             worklist,
                             depths)) {
                return std::nullopt;
            }
            for (const SwitchTarget& target : instruction.switch_targets) {
                const auto successor = resolve(target.target_pc);
                if (!successor.has_value() ||
                    !merge_depth(*successor,
                                 output_depth,
                                 worklist,
                                 depths)) {
                    return std::nullopt;
                }
            }
            continue;
        }

        const auto successor = resolve(instruction.next_pc);
        if (!successor.has_value() ||
            !merge_depth(*successor, output_depth, worklist, depths)) {
            return std::nullopt;
        }
    }

    return depths;
}

struct InstructionOptimization final {
    std::optional<i32> folded_result;
    std::optional<i32> right_constant;
    std::optional<bool> branch_taken;
    std::optional<usize> forced_target;
    bool elide_push {false};
};

struct OptimizationPlan final {
    std::vector<InstructionOptimization> instructions;
    std::vector<u32> block_budget_costs;
    bool contains_loop {false};
    u64 propagated_constants {0};
    u64 folded_operations {0};
    u64 folded_branches {0};
    u64 budget_checks_elided {0};
};

[[nodiscard]] i32 wrapping_add(i32 left, i32 right) noexcept {
    return static_cast<i32>(static_cast<u32>(left) +
                            static_cast<u32>(right));
}

[[nodiscard]] i32 wrapping_subtract(i32 left, i32 right) noexcept {
    return static_cast<i32>(static_cast<u32>(left) -
                            static_cast<u32>(right));
}

[[nodiscard]] i32 wrapping_multiply(i32 left, i32 right) noexcept {
    return static_cast<i32>(static_cast<u32>(left) *
                            static_cast<u32>(right));
}

[[nodiscard]] i32 wrapping_negate(i32 value) noexcept {
    return static_cast<i32>(0U - static_cast<u32>(value));
}

[[nodiscard]] bool evaluate_integer_branch(u8 opcode,
                                           i32 left,
                                           i32 right = 0) noexcept {
    switch (opcode) {
    case 0x99U:
    case 0x9FU:
        return left == right;
    case 0x9AU:
    case 0xA0U:
        return left != right;
    case 0x9BU:
    case 0xA1U:
        return left < right;
    case 0x9CU:
    case 0xA2U:
        return left >= right;
    case 0x9DU:
    case 0xA3U:
        return left > right;
    case 0x9EU:
    case 0xA4U:
        return left <= right;
    default:
        return false;
    }
}

[[nodiscard]] std::optional<i32> pushed_constant(
    const DecodedInstruction& instruction) noexcept {
    if (instruction.opcode == 0x01U) return 0;
    if (instruction.opcode >= 0x02U && instruction.opcode <= 0x08U) {
        return static_cast<i32>(instruction.opcode) - 3;
    }
    if (instruction.opcode == 0x10U || instruction.opcode == 0x11U ||
        instruction.opcode == 0x12U || instruction.opcode == 0x13U) {
        return instruction.immediate;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<i32> fold_binary_integer(u8 opcode,
                                                     i32 left,
                                                     i32 right) noexcept {
    switch (opcode) {
    case 0x60U:
        return wrapping_add(left, right);
    case 0x64U:
        return wrapping_subtract(left, right);
    case 0x68U:
        return wrapping_multiply(left, right);
    case 0x6CU:
        if (right == 0) return std::nullopt;
        if (left == std::numeric_limits<i32>::min() && right == -1) {
            return std::numeric_limits<i32>::min();
        }
        return left / right;
    case 0x70U:
        if (right == 0) return std::nullopt;
        if (left == std::numeric_limits<i32>::min() && right == -1) {
            return 0;
        }
        return left % right;
    case 0x78U:
        return static_cast<i32>(static_cast<u32>(left) <<
                                (static_cast<u32>(right) & 31U));
    case 0x7AU:
        return left >> (static_cast<u32>(right) & 31U);
    case 0x7CU:
        return static_cast<i32>(static_cast<u32>(left) >>
                                (static_cast<u32>(right) & 31U));
    case 0x7EU:
        return left & right;
    case 0x80U:
        return left | right;
    case 0x82U:
        return left ^ right;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<i32> fold_unary_integer(u8 opcode,
                                                    i32 value) noexcept {
    switch (opcode) {
    case 0x74U:
        return wrapping_negate(value);
    case 0x91U:
        return static_cast<i32>(static_cast<i8>(value));
    case 0x92U:
        return static_cast<i32>(static_cast<u16>(value));
    case 0x93U:
        return static_cast<i32>(static_cast<i16>(value));
    default:
        return std::nullopt;
    }
}

[[nodiscard]] OptimizationPlan build_optimization_plan(
    const std::vector<DecodedInstruction>& instructions,
    const std::vector<std::optional<u32>>& depths,
    u32 local_slots) {
    OptimizationPlan plan;
    plan.instructions.resize(instructions.size());
    if (instructions.empty()) return plan;

    std::unordered_map<usize, usize> index_by_pc;
    index_by_pc.reserve(instructions.size());
    for (usize index = 0; index < instructions.size(); ++index) {
        index_by_pc.emplace(instructions[index].pc, index);
    }

    std::vector<bool> block_boundary(instructions.size(), false);
    block_boundary[0] = true;
    const auto mark_target = [&](std::optional<usize> target) {
        if (!target.has_value()) return;
        const auto found = index_by_pc.find(*target);
        if (found != index_by_pc.end()) block_boundary[found->second] = true;
    };

    for (usize index = 0; index < instructions.size(); ++index) {
        const DecodedInstruction& instruction = instructions[index];
        mark_target(instruction.branch_target);
        mark_target(instruction.default_target);
        for (const SwitchTarget& target : instruction.switch_targets) {
            mark_target(target.target_pc);
            if (target.target_pc <= instruction.pc) plan.contains_loop = true;
        }
        if (instruction.branch_target.has_value() &&
            *instruction.branch_target <= instruction.pc) {
            plan.contains_loop = true;
        }
        if (index + 1U < instructions.size() &&
            (is_conditional_branch(instruction.opcode) ||
             is_goto(instruction.opcode) ||
             instruction.opcode == 0xAAU || instruction.opcode == 0xABU ||
             instruction.opcode == 0xACU || instruction.opcode == 0xB0U ||
             instruction.opcode == 0xB1U)) {
            block_boundary[index + 1U] = true;
        }
    }

    plan.block_budget_costs.assign(instructions.size(), 0U);
    u64 reachable_instructions = 0U;
    u64 emitted_budget_guards = 0U;
    for (const auto& depth : depths) {
        if (depth.has_value()) ++reachable_instructions;
    }
    for (usize start = 0; start < instructions.size(); ++start) {
        if (!block_boundary[start] || !depths[start].has_value()) continue;
        u32 cost = 0U;
        for (usize cursor = start;
             cursor < instructions.size() &&
             (cursor == start || !block_boundary[cursor]);
             ++cursor) {
            if (depths[cursor].has_value()) ++cost;
        }
        plan.block_budget_costs[start] = cost;
        emitted_budget_guards +=
            (static_cast<u64>(cost) + 4'094U) / 4'095U;
    }
    if (reachable_instructions > emitted_budget_guards) {
        plan.budget_checks_elided =
            reachable_instructions - emitted_budget_guards;
    }

    std::vector<std::optional<i32>> locals(local_slots);
    std::vector<std::optional<i32>> stack;
    for (usize index = 0; index < instructions.size(); ++index) {
        if (!depths[index].has_value()) continue;
        const u32 expected_depth = *depths[index];
        if (block_boundary[index] || stack.size() != expected_depth) {
            locals.assign(local_slots, std::nullopt);
            stack.assign(expected_depth, std::nullopt);
        }

        InstructionOptimization& optimization = plan.instructions[index];
        const DecodedInstruction& instruction = instructions[index];
        const auto pop_value = [&]() -> std::optional<i32> {
            if (stack.empty()) return std::nullopt;
            std::optional<i32> value = stack.back();
            stack.pop_back();
            return value;
        };
        const auto push_value = [&](std::optional<i32> value) {
            stack.push_back(value);
        };

        if (const auto constant = pushed_constant(instruction);
            constant.has_value()) {
            push_value(*constant);
            continue;
        }

        switch (instruction.opcode) {
        case 0x00U:
            break;
        case 0x15U:
        case 0x1AU:
        case 0x1BU:
        case 0x1CU:
        case 0x1DU: {
            const u32 local = instruction.opcode == 0x15U
                ? instruction.local_index
                : static_cast<u32>(instruction.opcode - 0x1AU);
            const std::optional<i32> value = local < locals.size()
                ? locals[local]
                : std::nullopt;
            optimization.folded_result = value;
            if (value.has_value()) ++plan.propagated_constants;
            push_value(value);
            break;
        }
        case 0x19U:
        case 0x2AU:
        case 0x2BU:
        case 0x2CU:
        case 0x2DU: {
            const u32 local = instruction.opcode == 0x19U
                ? instruction.local_index
                : static_cast<u32>(instruction.opcode - 0x2AU);
            push_value(local < locals.size() ? locals[local] : std::nullopt);
            break;
        }
        case 0x36U:
        case 0x3BU:
        case 0x3CU:
        case 0x3DU:
        case 0x3EU: {
            const u32 local = instruction.opcode == 0x36U
                ? instruction.local_index
                : static_cast<u32>(instruction.opcode - 0x3BU);
            const std::optional<i32> value = pop_value();
            if (local < locals.size()) locals[local] = value;
            break;
        }
        case 0x3AU:
        case 0x4BU:
        case 0x4CU:
        case 0x4DU:
        case 0x4EU: {
            const u32 local = instruction.opcode == 0x3AU
                ? instruction.local_index
                : static_cast<u32>(instruction.opcode - 0x4BU);
            const std::optional<i32> value = pop_value();
            if (local < locals.size()) locals[local] = value;
            break;
        }
        case 0x57U:
            (void)pop_value();
            break;
        case 0x58U:
            (void)pop_value();
            (void)pop_value();
            break;
        case 0x59U: {
            const std::optional<i32> first = pop_value();
            push_value(first);
            push_value(first);
            break;
        }
        case 0x5AU: {
            const std::optional<i32> first = pop_value();
            const std::optional<i32> second = pop_value();
            push_value(first);
            push_value(second);
            push_value(first);
            break;
        }
        case 0x5BU: {
            const std::optional<i32> first = pop_value();
            const std::optional<i32> second = pop_value();
            const std::optional<i32> third = pop_value();
            push_value(first);
            push_value(third);
            push_value(second);
            push_value(first);
            break;
        }
        case 0x5CU: {
            const std::optional<i32> first = pop_value();
            const std::optional<i32> second = pop_value();
            push_value(second);
            push_value(first);
            push_value(second);
            push_value(first);
            break;
        }
        case 0x5DU: {
            const std::optional<i32> first = pop_value();
            const std::optional<i32> second = pop_value();
            const std::optional<i32> third = pop_value();
            push_value(second);
            push_value(first);
            push_value(third);
            push_value(second);
            push_value(first);
            break;
        }
        case 0x5EU: {
            const std::optional<i32> first = pop_value();
            const std::optional<i32> second = pop_value();
            const std::optional<i32> third = pop_value();
            const std::optional<i32> fourth = pop_value();
            push_value(second);
            push_value(first);
            push_value(fourth);
            push_value(third);
            push_value(second);
            push_value(first);
            break;
        }
        case 0x5FU: {
            const std::optional<i32> first = pop_value();
            const std::optional<i32> second = pop_value();
            push_value(first);
            push_value(second);
            break;
        }
        case 0x60U:
        case 0x64U:
        case 0x68U:
        case 0x6CU:
        case 0x70U:
        case 0x78U:
        case 0x7AU:
        case 0x7CU:
        case 0x7EU:
        case 0x80U:
        case 0x82U: {
            const std::optional<i32> right = pop_value();
            const std::optional<i32> left = pop_value();
            optimization.right_constant = right;
            if (left.has_value() && right.has_value()) {
                optimization.folded_result = fold_binary_integer(
                    instruction.opcode, *left, *right);
                if (optimization.folded_result.has_value()) {
                    ++plan.folded_operations;
                }
            }
            push_value(optimization.folded_result);
            break;
        }
        case 0x74U:
        case 0x91U:
        case 0x92U:
        case 0x93U: {
            const std::optional<i32> value = pop_value();
            if (value.has_value()) {
                optimization.folded_result = fold_unary_integer(
                    instruction.opcode, *value);
                if (optimization.folded_result.has_value()) {
                    ++plan.folded_operations;
                }
            }
            push_value(optimization.folded_result);
            break;
        }
        case 0x84U:
            if (instruction.local_index < locals.size() &&
                locals[instruction.local_index].has_value()) {
                locals[instruction.local_index] = wrapping_add(
                    *locals[instruction.local_index], instruction.immediate);
                ++plan.propagated_constants;
            } else if (instruction.local_index < locals.size()) {
                locals[instruction.local_index] = std::nullopt;
            }
            break;
        case 0x99U:
        case 0x9AU:
        case 0x9BU:
        case 0x9CU:
        case 0x9DU:
        case 0x9EU: {
            const std::optional<i32> value = pop_value();
            if (value.has_value()) {
                optimization.branch_taken = evaluate_integer_branch(
                    instruction.opcode, *value);
                ++plan.folded_branches;
            }
            break;
        }
        case 0x9FU:
        case 0xA0U:
        case 0xA1U:
        case 0xA2U:
        case 0xA3U:
        case 0xA4U: {
            const std::optional<i32> right = pop_value();
            const std::optional<i32> left = pop_value();
            if (left.has_value() && right.has_value()) {
                optimization.branch_taken = evaluate_integer_branch(
                    instruction.opcode, *left, *right);
                ++plan.folded_branches;
            }
            break;
        }
        case 0xA5U:
        case 0xA6U: {
            const std::optional<i32> right = pop_value();
            const std::optional<i32> left = pop_value();
            if (left.has_value() && right.has_value()) {
                optimization.branch_taken = instruction.opcode == 0xA5U
                    ? *left == *right
                    : *left != *right;
                ++plan.folded_branches;
            }
            break;
        }
        case 0xC6U:
        case 0xC7U: {
            const std::optional<i32> value = pop_value();
            if (value.has_value()) {
                optimization.branch_taken = instruction.opcode == 0xC6U
                    ? *value == 0
                    : *value != 0;
                ++plan.folded_branches;
            }
            break;
        }
        case 0xAAU:
        case 0xABU: {
            const std::optional<i32> key = pop_value();
            if (key.has_value() && instruction.default_target.has_value()) {
                usize target_pc = *instruction.default_target;
                for (const SwitchTarget& target : instruction.switch_targets) {
                    if (target.key == *key) {
                        target_pc = target.target_pc;
                        break;
                    }
                }
                optimization.forced_target = target_pc;
                ++plan.folded_branches;
            }
            break;
        }
        case 0xA7U:
        case 0xC8U:
        case 0xB1U:
            break;
        case 0xACU:
        case 0xB0U:
            (void)pop_value();
            break;
        default:
            break;
        }
    }

    // A constant immediately consumed as the right operand never needs a
    // native stack store: the optimized consumer materializes it directly.
    for (usize index = 0; index + 1U < instructions.size(); ++index) {
        const auto constant = pushed_constant(instructions[index]);
        const InstructionOptimization& consumer = plan.instructions[index + 1U];
        if (constant.has_value() && consumer.right_constant.has_value() &&
            *constant == *consumer.right_constant &&
            !block_boundary[index + 1U]) {
            plan.instructions[index].elide_push = true;
        }
    }

    return plan;
}

[[nodiscard]] CompileAttempt compile_integer_method(
    const classfile::ClassFile& owner,
    const classfile::Method& method,
    const CachedMethodDescriptor& descriptor,
    bool has_receiver) {
    if (!method.code.has_value() ||
        (!integer_like(descriptor.return_kind) &&
         descriptor.return_kind != JavaTypeKind::reference &&
         descriptor.return_kind != JavaTypeKind::array &&
         descriptor.return_kind != JavaTypeKind::void_type)) {
        return {};
    }
    for (const TypeDescriptor& parameter : descriptor.descriptor.parameters) {
        if (!integer_like(parameter.kind) && !parameter.reference_like()) {
            return {};
        }
    }
    if (!method.code->exception_table.empty()) return {};

    const u32 local_slots = method.code->max_locals;
    const u32 stack_slots = method.code->max_stack;
    const usize parameter_base = has_receiver ? 1U : 0U;
    if (descriptor.descriptor.parameters.size() + parameter_base > local_slots ||
        local_slots > 1'000U || stack_slots > 1'000U ||
        local_slots > std::numeric_limits<u32>::max() - stack_slots) {
        return {};
    }

    const u32 total_slots = local_slots + stack_slots;
    const u32 raw_frame_size = total_slots * 8U;
    const u32 aligned_frame_size = (raw_frame_size + 15U) & ~15U;
    const u32 frame_size = std::max(16U, aligned_frame_size);
    if (frame_size > 4'080U) return {};

    auto decoded = decode_method(owner, method);
    if (!decoded.has_value()) return {};
    auto depths = compute_stack_depths(
        *decoded, local_slots, stack_slots, descriptor.return_kind);
    if (!depths.has_value()) return {};
    OptimizationPlan optimization_plan = build_optimization_plan(
        *decoded, *depths, local_slots);
    u64 strength_reductions = 0U;

    Arm64Emitter emitter;
    emitter.sub_sp(frame_size);
    emitter.move_w(kBudgetInitial, 1U);
    emitter.move_w(kBudgetRemaining, 1U);
    emitter.move_x(kResultPointer, 2U);

    const usize argument_count =
        descriptor.descriptor.parameters.size() + parameter_base;
    for (usize index = 0; index < argument_count; ++index) {
        const u32 argument_offset = static_cast<u32>(index) * 8U;
        const u32 local_slot_offset = static_cast<u32>(index) * 8U;
        emitter.load_x(kScratchLeft, 0U, argument_offset);
        emitter.store_x(kScratchLeft, kStackPointer, local_slot_offset);
    }

    const auto local_offset = [](u32 index) noexcept { return index * 8U; };
    const auto stack_offset = [local_slots](u32 depth) noexcept {
        return (local_slots + depth) * 8U;
    };

    std::unordered_map<usize, usize> native_position_by_pc;
    native_position_by_pc.reserve(decoded->size());
    std::vector<BranchPatch> branch_patches;
    std::vector<DeoptPatch> deopt_patches;
    std::vector<usize> budget_patches;

    const auto emit_budget_guard = [&](u32 cost) {
        while (cost != 0U) {
            const u32 chunk = std::min(cost, 4'095U);
            emitter.compare_imm_w(kBudgetRemaining, chunk);
            budget_patches.push_back(
                emitter.emit_conditional_branch_placeholder(
                    Arm64Condition::unsigned_lower));
            emitter.sub_imm_w(kBudgetRemaining,
                              kBudgetRemaining,
                              chunk);
            cost -= chunk;
        }
    };

    const auto emit_normal_return = [&](bool has_value,
                                        u32 result_register) {
        if (has_value) {
            emitter.store_x(result_register, kResultPointer, 0U);
        } else {
            emitter.move_imm32(kScratchLeft, 0U);
            emitter.store_x(kScratchLeft, kResultPointer, 0U);
        }
        emitter.sub_w(kScratchThird, kBudgetInitial, kBudgetRemaining);
        emitter.move_imm32(0U, 0U);
        emitter.or_x_shifted(0U, 0U, kScratchThird, 32U);
        emitter.add_sp(frame_size);
        emitter.ret();
    };

    for (usize instruction_index = 0;
         instruction_index < decoded->size();
         ++instruction_index) {
        if (!(*depths)[instruction_index].has_value()) continue;
        const DecodedInstruction& instruction = (*decoded)[instruction_index];
        const InstructionOptimization& optimization =
            optimization_plan.instructions[instruction_index];
        native_position_by_pc.insert_or_assign(
            instruction.pc, emitter.position());
        emit_budget_guard(
            optimization_plan.block_budget_costs[instruction_index]);

        u32 depth = *(*depths)[instruction_index];
        const auto push_register = [&](u32 source) -> bool {
            if (depth >= stack_slots) return false;
            emitter.store_w(source, kStackPointer, stack_offset(depth));
            ++depth;
            return true;
        };
        const auto pop_register = [&](u32 target) -> bool {
            if (depth == 0U) return false;
            --depth;
            emitter.load_w(target, kStackPointer, stack_offset(depth));
            return true;
        };
        const auto push_reference_register = [&](u32 source) -> bool {
            if (depth >= stack_slots) return false;
            emitter.store_x(source, kStackPointer, stack_offset(depth));
            ++depth;
            return true;
        };
        const auto pop_reference_register = [&](u32 target) -> bool {
            if (depth == 0U) return false;
            --depth;
            emitter.load_x(target, kStackPointer, stack_offset(depth));
            return true;
        };
        const auto emit_binary = [&](auto operation) -> bool {
            if (!pop_register(kScratchRight) ||
                !pop_register(kScratchLeft)) {
                return false;
            }
            operation(kScratchLeft, kScratchLeft, kScratchRight);
            return push_register(kScratchLeft);
        };
        const auto replace_stack_with_constant = [&](u32 popped,
                                                      i32 value) -> bool {
            if (depth < popped) return false;
            depth -= popped;
            emitter.move_imm32(kScratchLeft, static_cast<u32>(value));
            return push_register(kScratchLeft);
        };
        const auto emit_right_constant_binary = [&](u8 opcode,
                                                    i32 constant) -> bool {
            if (depth < 2U ||
                ((opcode == 0x6CU || opcode == 0x70U) && constant == 0)) {
                return false;
            }
            depth -= 2U;
            emitter.load_w(kScratchLeft,
                           kStackPointer,
                           stack_offset(depth));

            switch (opcode) {
            case 0x60U:
                if (constant != 0) {
                    emitter.move_imm32(kScratchRight,
                                       static_cast<u32>(constant));
                    emitter.add_w(kScratchLeft,
                                  kScratchLeft,
                                  kScratchRight);
                }
                break;
            case 0x64U:
                if (constant != 0) {
                    emitter.move_imm32(kScratchRight,
                                       static_cast<u32>(constant));
                    emitter.sub_w(kScratchLeft,
                                  kScratchLeft,
                                  kScratchRight);
                }
                break;
            case 0x68U: {
                const u32 magnitude = constant < 0
                    ? 0U - static_cast<u32>(constant)
                    : static_cast<u32>(constant);
                if (constant == 0) {
                    emitter.move_imm32(kScratchLeft, 0U);
                } else if (constant == 1) {
                    // Identity.
                } else if (constant == -1) {
                    emitter.negate_w(kScratchLeft, kScratchLeft);
                } else if (std::has_single_bit(magnitude)) {
                    emitter.move_imm32(
                        kScratchRight,
                        static_cast<u32>(std::countr_zero(magnitude)));
                    emitter.shift_left_w(kScratchLeft,
                                         kScratchLeft,
                                         kScratchRight);
                    if (constant < 0) {
                        emitter.negate_w(kScratchLeft, kScratchLeft);
                    }
                } else {
                    emitter.move_imm32(kScratchRight,
                                       static_cast<u32>(constant));
                    emitter.multiply_w(kScratchLeft,
                                       kScratchLeft,
                                       kScratchRight);
                }
                break;
            }
            case 0x6CU:
                if (constant == 1) {
                    // Identity.
                } else if (constant == -1) {
                    emitter.negate_w(kScratchLeft, kScratchLeft);
                } else {
                    emitter.move_imm32(kScratchRight,
                                       static_cast<u32>(constant));
                    emitter.divide_signed_w(kScratchLeft,
                                            kScratchLeft,
                                            kScratchRight);
                }
                break;
            case 0x70U:
                if (constant == 1 || constant == -1) {
                    emitter.move_imm32(kScratchLeft, 0U);
                } else {
                    emitter.move_imm32(kScratchRight,
                                       static_cast<u32>(constant));
                    emitter.divide_signed_w(kScratchThird,
                                            kScratchLeft,
                                            kScratchRight);
                    emitter.multiply_subtract_w(kScratchLeft,
                                                kScratchThird,
                                                kScratchRight,
                                                kScratchLeft);
                }
                break;
            case 0x78U:
            case 0x7AU:
            case 0x7CU:
                emitter.move_imm32(
                    kScratchRight, static_cast<u32>(constant) & 31U);
                if (opcode == 0x78U) {
                    emitter.shift_left_w(kScratchLeft,
                                         kScratchLeft,
                                         kScratchRight);
                } else if (opcode == 0x7AU) {
                    emitter.shift_right_arithmetic_w(kScratchLeft,
                                                     kScratchLeft,
                                                     kScratchRight);
                } else {
                    emitter.shift_right_logical_w(kScratchLeft,
                                                  kScratchLeft,
                                                  kScratchRight);
                }
                break;
            case 0x7EU:
                if (constant == 0) {
                    emitter.move_imm32(kScratchLeft, 0U);
                } else if (constant != -1) {
                    emitter.move_imm32(kScratchRight,
                                       static_cast<u32>(constant));
                    emitter.and_w(kScratchLeft,
                                  kScratchLeft,
                                  kScratchRight);
                }
                break;
            case 0x80U:
                if (constant == -1) {
                    emitter.move_imm32(kScratchLeft, 0xFFFF'FFFFU);
                } else if (constant != 0) {
                    emitter.move_imm32(kScratchRight,
                                       static_cast<u32>(constant));
                    emitter.or_w(kScratchLeft,
                                 kScratchLeft,
                                 kScratchRight);
                }
                break;
            case 0x82U:
                if (constant != 0) {
                    emitter.move_imm32(kScratchRight,
                                       static_cast<u32>(constant));
                    emitter.xor_w(kScratchLeft,
                                  kScratchLeft,
                                  kScratchRight);
                }
                break;
            default:
                return false;
            }
            ++strength_reductions;
            return push_register(kScratchLeft);
        };

        switch (instruction.opcode) {
        case 0x00U:
            break;
        case 0x01U:
            emitter.move_imm32(kScratchLeft, 0U);
            if (!push_reference_register(kScratchLeft)) return {};
            break;
        case 0x02U:
        case 0x03U:
        case 0x04U:
        case 0x05U:
        case 0x06U:
        case 0x07U:
        case 0x08U: {
            const i32 value = static_cast<i32>(instruction.opcode) - 3;
            if (optimization.elide_push) {
                if (depth >= stack_slots) return {};
                ++depth;
            } else {
                emitter.move_imm32(kScratchLeft, static_cast<u32>(value));
                if (!push_register(kScratchLeft)) return {};
            }
            break;
        }
        case 0x10U:
        case 0x11U:
        case 0x12U:
        case 0x13U:
            if (optimization.elide_push) {
                if (depth >= stack_slots) return {};
                ++depth;
            } else {
                emitter.move_imm32(
                    kScratchLeft, static_cast<u32>(instruction.immediate));
                if (!push_register(kScratchLeft)) return {};
            }
            break;
        case 0x15U:
            if (optimization.folded_result.has_value()) {
                emitter.move_imm32(
                    kScratchLeft,
                    static_cast<u32>(*optimization.folded_result));
            } else {
                emitter.load_w(kScratchLeft,
                               kStackPointer,
                               local_offset(instruction.local_index));
            }
            if (!push_register(kScratchLeft)) return {};
            break;
        case 0x1AU:
        case 0x1BU:
        case 0x1CU:
        case 0x1DU: {
            const u32 index = static_cast<u32>(instruction.opcode - 0x1AU);
            if (optimization.folded_result.has_value()) {
                emitter.move_imm32(
                    kScratchLeft,
                    static_cast<u32>(*optimization.folded_result));
            } else {
                emitter.load_w(kScratchLeft,
                               kStackPointer,
                               local_offset(index));
            }
            if (!push_register(kScratchLeft)) return {};
            break;
        }
        case 0x19U:
            emitter.load_x(kScratchLeft,
                           kStackPointer,
                           local_offset(instruction.local_index));
            if (!push_reference_register(kScratchLeft)) return {};
            break;
        case 0x2AU:
        case 0x2BU:
        case 0x2CU:
        case 0x2DU: {
            const u32 index = static_cast<u32>(instruction.opcode - 0x2AU);
            emitter.load_x(kScratchLeft, kStackPointer, local_offset(index));
            if (!push_reference_register(kScratchLeft)) return {};
            break;
        }
        case 0x36U:
            if (!pop_register(kScratchLeft)) return {};
            emitter.store_w(kScratchLeft,
                            kStackPointer,
                            local_offset(instruction.local_index));
            break;
        case 0x3BU:
        case 0x3CU:
        case 0x3DU:
        case 0x3EU: {
            const u32 index = static_cast<u32>(instruction.opcode - 0x3BU);
            if (!pop_register(kScratchLeft)) return {};
            emitter.store_w(kScratchLeft, kStackPointer, local_offset(index));
            break;
        }
        case 0x3AU:
            if (!pop_reference_register(kScratchLeft)) return {};
            emitter.store_x(kScratchLeft,
                            kStackPointer,
                            local_offset(instruction.local_index));
            break;
        case 0x4BU:
        case 0x4CU:
        case 0x4DU:
        case 0x4EU: {
            const u32 index = static_cast<u32>(instruction.opcode - 0x4BU);
            if (!pop_reference_register(kScratchLeft)) return {};
            emitter.store_x(kScratchLeft, kStackPointer, local_offset(index));
            break;
        }
        case 0x57U:
            if (depth == 0U) return {};
            --depth;
            break;
        case 0x58U:
            if (depth < 2U) return {};
            depth -= 2U;
            break;
        case 0x59U:
            if (depth == 0U || depth >= stack_slots) return {};
            emitter.load_x(kScratchLeft,
                           kStackPointer,
                           stack_offset(depth - 1U));
            if (!push_reference_register(kScratchLeft)) return {};
            break;
        case 0x5AU:
            if (depth < 2U || depth >= stack_slots) return {};
            emitter.load_x(kScratchLeft, kStackPointer, stack_offset(depth - 1U));
            emitter.load_x(kScratchRight, kStackPointer, stack_offset(depth - 2U));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth - 2U));
            emitter.store_x(kScratchRight, kStackPointer, stack_offset(depth - 1U));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth));
            ++depth;
            break;
        case 0x5BU:
            if (depth < 3U || depth >= stack_slots) return {};
            emitter.load_x(kScratchLeft, kStackPointer, stack_offset(depth - 1U));
            emitter.load_x(kScratchRight, kStackPointer, stack_offset(depth - 2U));
            emitter.load_x(kScratchThird, kStackPointer, stack_offset(depth - 3U));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth - 3U));
            emitter.store_x(kScratchThird, kStackPointer, stack_offset(depth - 2U));
            emitter.store_x(kScratchRight, kStackPointer, stack_offset(depth - 1U));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth));
            ++depth;
            break;
        case 0x5CU:
            if (depth < 2U || depth + 2U > stack_slots) return {};
            emitter.load_x(kScratchLeft, kStackPointer, stack_offset(depth - 2U));
            emitter.load_x(kScratchRight, kStackPointer, stack_offset(depth - 1U));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth));
            emitter.store_x(kScratchRight, kStackPointer, stack_offset(depth + 1U));
            depth += 2U;
            break;
        case 0x5DU:
            if (depth < 3U || depth + 2U > stack_slots) return {};
            emitter.load_x(kScratchLeft, kStackPointer, stack_offset(depth - 1U));
            emitter.load_x(kScratchRight, kStackPointer, stack_offset(depth - 2U));
            emitter.load_x(kScratchThird, kStackPointer, stack_offset(depth - 3U));
            emitter.store_x(kScratchRight, kStackPointer, stack_offset(depth - 3U));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth - 2U));
            emitter.store_x(kScratchThird, kStackPointer, stack_offset(depth - 1U));
            emitter.store_x(kScratchRight, kStackPointer, stack_offset(depth));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth + 1U));
            depth += 2U;
            break;
        case 0x5EU:
            if (depth < 4U || depth + 2U > stack_slots) return {};
            emitter.load_x(kScratchLeft, kStackPointer, stack_offset(depth - 1U));
            emitter.load_x(kScratchRight, kStackPointer, stack_offset(depth - 2U));
            emitter.load_x(kScratchThird, kStackPointer, stack_offset(depth - 3U));
            emitter.load_x(kScratchFourth, kStackPointer, stack_offset(depth - 4U));
            emitter.store_x(kScratchRight, kStackPointer, stack_offset(depth - 4U));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth - 3U));
            emitter.store_x(kScratchFourth, kStackPointer, stack_offset(depth - 2U));
            emitter.store_x(kScratchThird, kStackPointer, stack_offset(depth - 1U));
            emitter.store_x(kScratchRight, kStackPointer, stack_offset(depth));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth + 1U));
            depth += 2U;
            break;
        case 0x5FU:
            if (depth < 2U) return {};
            emitter.load_x(kScratchLeft, kStackPointer, stack_offset(depth - 2U));
            emitter.load_x(kScratchRight, kStackPointer, stack_offset(depth - 1U));
            emitter.store_x(kScratchRight, kStackPointer, stack_offset(depth - 2U));
            emitter.store_x(kScratchLeft, kStackPointer, stack_offset(depth - 1U));
            break;
        case 0x60U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.add_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x64U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.sub_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x68U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.multiply_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x6CU:
        case 0x70U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
                break;
            }
            if (optimization.right_constant.has_value() &&
                *optimization.right_constant != 0) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
                break;
            }
            if (!pop_register(kScratchRight) ||
                !pop_register(kScratchLeft)) {
                return {};
            }
            deopt_patches.push_back(DeoptPatch {
                .native_index = emitter.emit_cbz_w_placeholder(kScratchRight),
                .register_index = kScratchRight,
            });
            emitter.divide_signed_w(
                kScratchThird, kScratchLeft, kScratchRight);
            if (instruction.opcode == 0x6CU) {
                emitter.move_w(kScratchLeft, kScratchThird);
            } else {
                emitter.multiply_subtract_w(kScratchLeft,
                                            kScratchThird,
                                            kScratchRight,
                                            kScratchLeft);
            }
            if (!push_register(kScratchLeft)) return {};
            break;
        case 0x74U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        1U, *optimization.folded_result)) return {};
            } else {
                if (!pop_register(kScratchLeft)) return {};
                emitter.negate_w(kScratchLeft, kScratchLeft);
                if (!push_register(kScratchLeft)) return {};
            }
            break;
        case 0x78U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.shift_left_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x7AU:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.shift_right_arithmetic_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x7CU:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.shift_right_logical_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x7EU:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.and_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x80U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.or_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x82U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        2U, *optimization.folded_result)) return {};
            } else if (optimization.right_constant.has_value()) {
                if (!emit_right_constant_binary(
                        instruction.opcode,
                        *optimization.right_constant)) return {};
            } else if (!emit_binary([&](u32 d, u32 l, u32 r) {
                           emitter.xor_w(d, l, r);
                       })) {
                return {};
            }
            break;
        case 0x84U:
            emitter.load_w(kScratchLeft,
                           kStackPointer,
                           local_offset(instruction.local_index));
            emitter.move_imm32(
                kScratchRight, static_cast<u32>(instruction.immediate));
            emitter.add_w(kScratchLeft, kScratchLeft, kScratchRight);
            emitter.store_w(kScratchLeft,
                            kStackPointer,
                            local_offset(instruction.local_index));
            break;
        case 0x91U:
        case 0x92U:
        case 0x93U:
            if (optimization.folded_result.has_value()) {
                if (!replace_stack_with_constant(
                        1U, *optimization.folded_result)) return {};
            } else {
                if (!pop_register(kScratchLeft)) return {};
                if (instruction.opcode == 0x91U) {
                    emitter.sign_extend_byte_w(kScratchLeft, kScratchLeft);
                } else if (instruction.opcode == 0x92U) {
                    emitter.zero_extend_half_w(kScratchLeft, kScratchLeft);
                } else {
                    emitter.sign_extend_half_w(kScratchLeft, kScratchLeft);
                }
                if (!push_register(kScratchLeft)) return {};
            }
            break;
        case 0x99U:
        case 0x9AU:
        case 0x9BU:
        case 0x9CU:
        case 0x9DU:
        case 0x9EU: {
            if (!instruction.branch_target.has_value()) return {};
            if (optimization.branch_taken.has_value()) {
                if (depth == 0U) return {};
                --depth;
                if (*optimization.branch_taken) {
                    branch_patches.push_back(BranchPatch {
                        .kind = BranchPatch::Kind::unconditional,
                        .native_index = emitter.emit_branch_placeholder(),
                        .target_pc = *instruction.branch_target,
                        .condition = Arm64Condition::equal,
                    });
                }
                break;
            }
            if (!pop_register(kScratchLeft)) return {};
            const auto condition = branch_condition(instruction.opcode);
            if (!condition.has_value()) return {};
            emitter.compare_zero_w(kScratchLeft);
            branch_patches.push_back(BranchPatch {
                .kind = BranchPatch::Kind::conditional,
                .native_index =
                    emitter.emit_conditional_branch_placeholder(*condition),
                .target_pc = *instruction.branch_target,
                .condition = *condition,
            });
            break;
        }
        case 0x9FU:
        case 0xA0U:
        case 0xA1U:
        case 0xA2U:
        case 0xA3U:
        case 0xA4U: {
            if (!instruction.branch_target.has_value()) return {};
            if (optimization.branch_taken.has_value()) {
                if (depth < 2U) return {};
                depth -= 2U;
                if (*optimization.branch_taken) {
                    branch_patches.push_back(BranchPatch {
                        .kind = BranchPatch::Kind::unconditional,
                        .native_index = emitter.emit_branch_placeholder(),
                        .target_pc = *instruction.branch_target,
                        .condition = Arm64Condition::equal,
                    });
                }
                break;
            }
            if (!pop_register(kScratchRight) ||
                !pop_register(kScratchLeft)) {
                return {};
            }
            const auto condition = branch_condition(instruction.opcode);
            if (!condition.has_value()) return {};
            emitter.compare_w(kScratchLeft, kScratchRight);
            branch_patches.push_back(BranchPatch {
                .kind = BranchPatch::Kind::conditional,
                .native_index =
                    emitter.emit_conditional_branch_placeholder(*condition),
                .target_pc = *instruction.branch_target,
                .condition = *condition,
            });
            break;
        }
        case 0xA5U:
        case 0xA6U: {
            if (!instruction.branch_target.has_value()) return {};
            if (optimization.branch_taken.has_value()) {
                if (depth < 2U) return {};
                depth -= 2U;
                if (*optimization.branch_taken) {
                    branch_patches.push_back(BranchPatch {
                        .kind = BranchPatch::Kind::unconditional,
                        .native_index = emitter.emit_branch_placeholder(),
                        .target_pc = *instruction.branch_target,
                        .condition = Arm64Condition::equal,
                    });
                }
                break;
            }
            if (!pop_reference_register(kScratchRight) ||
                !pop_reference_register(kScratchLeft)) {
                return {};
            }
            const auto condition = branch_condition(instruction.opcode);
            if (!condition.has_value()) return {};
            emitter.compare_x(kScratchLeft, kScratchRight);
            branch_patches.push_back(BranchPatch {
                .kind = BranchPatch::Kind::conditional,
                .native_index =
                    emitter.emit_conditional_branch_placeholder(*condition),
                .target_pc = *instruction.branch_target,
                .condition = *condition,
            });
            break;
        }
        case 0xC6U:
        case 0xC7U: {
            if (!instruction.branch_target.has_value()) return {};
            if (optimization.branch_taken.has_value()) {
                if (depth == 0U) return {};
                --depth;
                if (*optimization.branch_taken) {
                    branch_patches.push_back(BranchPatch {
                        .kind = BranchPatch::Kind::unconditional,
                        .native_index = emitter.emit_branch_placeholder(),
                        .target_pc = *instruction.branch_target,
                        .condition = Arm64Condition::equal,
                    });
                }
                break;
            }
            if (!pop_reference_register(kScratchLeft)) return {};
            const auto condition = branch_condition(instruction.opcode);
            if (!condition.has_value()) return {};
            emitter.compare_zero_x(kScratchLeft);
            branch_patches.push_back(BranchPatch {
                .kind = BranchPatch::Kind::conditional,
                .native_index =
                    emitter.emit_conditional_branch_placeholder(*condition),
                .target_pc = *instruction.branch_target,
                .condition = *condition,
            });
            break;
        }
        case 0xA7U:
        case 0xC8U:
            if (!instruction.branch_target.has_value()) return {};
            branch_patches.push_back(BranchPatch {
                .kind = BranchPatch::Kind::unconditional,
                .native_index = emitter.emit_branch_placeholder(),
                .target_pc = *instruction.branch_target,
                .condition = Arm64Condition::equal,
            });
            break;
        case 0xAAU:
        case 0xABU:
            if (!instruction.default_target.has_value()) return {};
            if (optimization.forced_target.has_value()) {
                if (depth == 0U) return {};
                --depth;
                branch_patches.push_back(BranchPatch {
                    .kind = BranchPatch::Kind::unconditional,
                    .native_index = emitter.emit_branch_placeholder(),
                    .target_pc = *optimization.forced_target,
                    .condition = Arm64Condition::equal,
                });
                break;
            }
            if (!pop_register(kScratchLeft)) return {};
            for (const SwitchTarget& target : instruction.switch_targets) {
                emitter.move_imm32(kScratchRight, static_cast<u32>(target.key));
                emitter.compare_w(kScratchLeft, kScratchRight);
                branch_patches.push_back(BranchPatch {
                    .kind = BranchPatch::Kind::conditional,
                    .native_index = emitter.emit_conditional_branch_placeholder(
                        Arm64Condition::equal),
                    .target_pc = target.target_pc,
                    .condition = Arm64Condition::equal,
                });
            }
            branch_patches.push_back(BranchPatch {
                .kind = BranchPatch::Kind::unconditional,
                .native_index = emitter.emit_branch_placeholder(),
                .target_pc = *instruction.default_target,
                .condition = Arm64Condition::equal,
            });
            break;
        case 0xACU:
            if (!pop_register(kScratchLeft) || depth != 0U) return {};
            emit_normal_return(true, kScratchLeft);
            break;
        case 0xB0U:
            if (!pop_reference_register(kScratchLeft) || depth != 0U) return {};
            emit_normal_return(true, kScratchLeft);
            break;
        case 0xB1U:
            if (depth != 0U) return {};
            emit_normal_return(false, kScratchLeft);
            break;
        default:
            return {};
        }

        const auto shape = stack_shape(instruction, descriptor.return_kind);
        if (!shape.has_value()) return {};
        const u32 expected_depth = *(*depths)[instruction_index] -
                                   shape->pop + shape->push;
        if (depth != expected_depth) return {};
    }

    const usize deopt_position = emitter.position();
    emitter.sub_w(kScratchThird, kBudgetInitial, kBudgetRemaining);
    emitter.move_deopt_marker_x0();
    emitter.or_x_shifted(0U, 0U, kScratchThird, 32U);
    emitter.add_sp(frame_size);
    emitter.ret();

    const usize budget_deopt_position = emitter.position();
    emitter.sub_w(kScratchThird, kBudgetInitial, kBudgetRemaining);
    emitter.move_budget_deopt_marker_x0();
    emitter.or_x_shifted(0U, 0U, kScratchThird, 32U);
    emitter.add_sp(frame_size);
    emitter.ret();

    for (const BranchPatch& patch : branch_patches) {
        const auto target = native_position_by_pc.find(patch.target_pc);
        if (target == native_position_by_pc.end()) return {};
        const bool patched = patch.kind == BranchPatch::Kind::unconditional
            ? emitter.patch_branch(patch.native_index, target->second)
            : emitter.patch_conditional_branch(
                patch.native_index, target->second, patch.condition);
        if (!patched) return {};
    }
    for (const DeoptPatch& patch : deopt_patches) {
        if (!emitter.patch_cbz_w(
                patch.native_index, deopt_position, patch.register_index)) {
            return {};
        }
    }
    for (const usize patch : budget_patches) {
        if (!emitter.patch_conditional_branch(
                patch,
                budget_deopt_position,
                Arm64Condition::unsigned_lower)) {
            return {};
        }
    }

    auto block = finalize_code(emitter.code());
    if (!block.has_value()) {
        return CompileAttempt {
            .disposition = CompileDisposition::executable_memory_unavailable,
            .method = std::nullopt,
        };
    }
    return CompileAttempt {
        .disposition = CompileDisposition::success,
        .method = CompiledMethod {
            .block = std::move(*block),
            .return_kind = descriptor.return_kind,
            .returns_value = descriptor.return_kind != JavaTypeKind::void_type,
            .optimized = optimization_plan.propagated_constants != 0U ||
                         optimization_plan.folded_operations != 0U ||
                         optimization_plan.folded_branches != 0U ||
                         strength_reductions != 0U,
            .contains_loop = optimization_plan.contains_loop,
            .propagated_constants = optimization_plan.propagated_constants,
            .folded_operations = optimization_plan.folded_operations,
            .folded_branches = optimization_plan.folded_branches,
            .strength_reductions = strength_reductions,
            .budget_checks_elided =
                optimization_plan.budget_checks_elided,
        },
    };
}

#endif

} // namespace

class BaselineJit::Impl final {
public:
    explicit Impl(bool initial_enabled)
        : enabled_(initial_enabled),
          availability_(initial_enabled
              ? probe_platform()
              : JitAvailability::unavailable),
          hot_threshold_(configured_hot_threshold()),
          code_cache_limit_bytes_(configured_code_cache_bytes()) {
        stats_.code_cache_limit_bytes = code_cache_limit_bytes_;
    }

    void set_enabled(bool value) noexcept {
        enabled_ = value;
        if (!enabled_) {
            clear_cache();
            availability_ = JitAvailability::unavailable;
            unavailable_probe_countdown_ = 0U;
            return;
        }
        refresh_availability();
    }

    void refresh_availability() noexcept {
        availability_ = probe_platform();
        unavailable_probe_countdown_ = availability_ == JitAvailability::ready
            ? 0U
            : kUnavailableProbeInterval;
    }

    void clear_cache() noexcept {
#if defined(__aarch64__)
        entries_.clear();
#endif
        code_cache_bytes_ = 0U;
        stats_.code_cache_bytes = 0U;
    }

    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    [[nodiscard]] JitAvailability availability() const noexcept {
        return availability_;
    }
    [[nodiscard]] JitStatistics statistics() const noexcept { return stats_; }

    [[nodiscard]] Result<std::optional<JitExecutionResult>> try_execute(
        MethodId method_id,
        const classfile::ClassFile& owner,
        const classfile::Method& method,
        const CachedMethodDescriptor& descriptor,
        std::span<const Value> arguments,
        bool has_receiver,
        u64 instruction_budget) {
        if (!enabled_ || !method_id.valid()) {
            return std::optional<JitExecutionResult>{};
        }
        if (availability_ != JitAvailability::ready) {
            if (unavailable_probe_countdown_ == 0U) {
                refresh_availability();
            } else {
                --unavailable_probe_countdown_;
            }
            if (availability_ != JitAvailability::ready) {
                return std::optional<JitExecutionResult>{};
            }
        }
#if !defined(__aarch64__)
        (void)owner;
        (void)method;
        (void)descriptor;
        (void)arguments;
        (void)has_receiver;
        (void)instruction_budget;
        return std::optional<JitExecutionResult>{};
#else
        if (instruction_budget >
            static_cast<u64>(kMaximumPackedBudget)) {
            // The compact native ABI stores the exact executed bytecode count
            // in 30 bits. Preserve scheduler semantics by using the interpreter
            // rather than truncating a larger long-lived budget.
            return std::optional<JitExecutionResult>{};
        }

        const usize expected_arguments =
            descriptor.descriptor.parameters.size() + (has_receiver ? 1U : 0U);
        if (arguments.size() != expected_arguments) {
            return std::optional<JitExecutionResult>{};
        }
        std::vector<u64> native_arguments;
        native_arguments.reserve(arguments.size());
        for (const Value& argument : arguments) {
            if (argument.kind() == ValueKind::int32) {
                auto value = argument.as_int();
                if (!value) return std::optional<JitExecutionResult>{};
                native_arguments.push_back(
                    static_cast<u64>(static_cast<u32>(*value)));
            } else if (argument.kind() == ValueKind::reference) {
                auto value = argument.as_reference();
                if (!value) return std::optional<JitExecutionResult>{};
                native_arguments.push_back(value->bits);
            } else {
                return std::optional<JitExecutionResult>{};
            }
        }

        Entry& entry = entries_[method_id];
        if ((entry.source_method != nullptr && entry.source_method != &method) ||
            (entry.source_owner != nullptr && entry.source_owner != &owner)) {
            release_compiled_entry(entry);
            entry = Entry {};
        }
        entry.source_method = &method;
        entry.source_owner = &owner;
        if (entry.rejected) return std::optional<JitExecutionResult>{};

        if (!entry.compiled.has_value()) {
            if (entry.compilation_threshold == 0U) {
                entry.compilation_threshold = hot_threshold_;
                if (auto decoded = decode_method(owner, method);
                    decoded.has_value()) {
                    for (const auto& instruction : *decoded) {
                        bool backward = instruction.branch_target.has_value() &&
                            *instruction.branch_target <= instruction.pc;
                        if (!backward) {
                            for (const SwitchTarget& target :
                                 instruction.switch_targets) {
                                if (target.target_pc <= instruction.pc) {
                                    backward = true;
                                    break;
                                }
                            }
                        }
                        if (backward) {
                            entry.loop_candidate = true;
                            entry.compilation_threshold = std::max(
                                1U,
                                std::min(hot_threshold_,
                                         std::max(4U,
                                                  hot_threshold_ / 4U)));
                            break;
                        }
                    }
                }
            }
            if (entry.observed_calls < entry.compilation_threshold) {
                ++entry.observed_calls;
                return std::optional<JitExecutionResult>{};
            }
            CompileAttempt attempt = compile_integer_method(
                owner, method, descriptor, has_receiver);
            if (attempt.disposition ==
                    CompileDisposition::executable_memory_unavailable) {
                refresh_availability();
                return std::optional<JitExecutionResult>{};
            }
            if (attempt.disposition != CompileDisposition::success ||
                !attempt.method.has_value()) {
                entry.rejected = true;
                ++stats_.rejected_methods;
                return std::optional<JitExecutionResult>{};
            }

            const u64 needed = attempt.method->block.mapped_size;
            if (needed > code_cache_limit_bytes_ ||
                !evict_for(needed, method_id)) {
                entry.rejected = true;
                ++stats_.rejected_methods;
                return std::optional<JitExecutionResult>{};
            }
            code_cache_bytes_ += needed;
            stats_.code_cache_bytes = code_cache_bytes_;
            entry.compiled = std::move(*attempt.method);
            entry.last_used_tick = ++use_tick_;
            ++stats_.compiled_methods;
            if (entry.compiled->optimized) ++stats_.optimized_methods;
            if (entry.compiled->contains_loop) {
                ++stats_.loop_optimized_methods;
            }
            stats_.propagated_constants +=
                entry.compiled->propagated_constants;
            stats_.folded_operations += entry.compiled->folded_operations;
            stats_.folded_branches += entry.compiled->folded_branches;
            stats_.strength_reductions +=
                entry.compiled->strength_reductions;
            stats_.budget_checks_elided +=
                entry.compiled->budget_checks_elided;
        }

        const u32 native_budget = static_cast<u32>(instruction_budget);
        const u64* argument_pointer = native_arguments.empty()
            ? nullptr
            : native_arguments.data();
        u64 result_bits = 0U;
        const u64 raw_result = entry.compiled->block.function(
            argument_pointer, native_budget, &result_bits);
        entry.last_used_tick = ++use_tick_;

        const u32 executed = static_cast<u32>(
            (raw_result >> 32U) & static_cast<u64>(kMaximumPackedBudget));
        if ((raw_result & kDeoptMarker) != 0U) {
            ++stats_.deoptimized_executions;
            if ((raw_result & kBudgetExhaustedMarker) != 0U) {
                return fail(
                    ErrorCode::invalid_state,
                    "JIT bytecode instruction budget was exhausted");
            }
            return std::optional<JitExecutionResult>{};
        }

        std::optional<Value> return_value;
        if (entry.compiled->returns_value) {
            if (integer_like(entry.compiled->return_kind)) {
                return_value = Value::from_int(
                    static_cast<i32>(static_cast<u32>(result_bits)));
            } else if (entry.compiled->return_kind == JavaTypeKind::reference ||
                       entry.compiled->return_kind == JavaTypeKind::array) {
                return_value = Value::from_reference(ObjectRef {result_bits});
            } else {
                return fail(ErrorCode::invalid_state,
                            "JIT returned an unsupported value kind");
            }
        }
        ++stats_.executed_methods;
        return std::optional<JitExecutionResult>(JitExecutionResult {
            .return_value = return_value,
            .bytecode_instructions = executed,
        });
#endif
    }

    [[nodiscard]] static JitAvailability probe_platform() noexcept {
#if !defined(__aarch64__)
        return JitAvailability::unavailable;
#else
#if defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
        // Never probe iOS JIT permission by executing a generated instruction.
        // On A12+ the kernel may terminate a non-debugged process at the first
        // unsigned instruction even when mmap/mprotect appeared to succeed.
        // The app bridge follows UTM and checks csflags/ptrace state instead.
        using PlatformStatusFunction = i32 (*)(void);
        static const auto platform_status =
            reinterpret_cast<PlatformStatusFunction>(
                ::dlsym(RTLD_DEFAULT, "phoneme_platform_jit_status"));
        return platform_status != nullptr && platform_status() != 0
            ? JitAvailability::ready
            : JitAvailability::unavailable;
#else
        static std::atomic<i32> cached {-1};
        const i32 known = cached.load(std::memory_order_acquire);
        if (known >= 0) return static_cast<JitAvailability>(known);

        const std::vector<u32> self_test_code {
            0x52800549U, // mov w9, #42
            0xF9000049U, // str x9, [x2]
            0x52800000U, // mov w0, #0
            0xD65F03C0U, // ret
        };
        auto self_test = finalize_code(self_test_code);
        u64 self_test_value = 0U;
        if (self_test.has_value() &&
            self_test->function != nullptr &&
            self_test->function(nullptr, 1U, &self_test_value) == 0U &&
            self_test_value == 42U) {
            cached.store(
                static_cast<i32>(JitAvailability::ready),
                std::memory_order_release);
            return JitAvailability::ready;
        }
        return JitAvailability::unavailable;
#endif
#endif
    }

private:
#if defined(__aarch64__)
    struct Entry final {
        const classfile::ClassFile* source_owner {nullptr};
        const classfile::Method* source_method {nullptr};
        u32 observed_calls {0};
        u32 compilation_threshold {0};
        bool rejected {false};
        bool loop_candidate {false};
        u64 last_used_tick {0};
        std::optional<CompiledMethod> compiled;
    };

    void release_compiled_entry(Entry& entry) noexcept {
        if (!entry.compiled.has_value()) return;
        const u64 released = entry.compiled->block.mapped_size;
        code_cache_bytes_ = released > code_cache_bytes_
            ? 0U
            : code_cache_bytes_ - released;
        stats_.code_cache_bytes = code_cache_bytes_;
        entry.compiled.reset();
    }

    [[nodiscard]] bool evict_for(u64 needed, MethodId protected_method) {
        while (code_cache_bytes_ + needed > code_cache_limit_bytes_) {
            auto candidate = entries_.end();
            for (auto current = entries_.begin(); current != entries_.end();
                 ++current) {
                if (current->first == protected_method ||
                    !current->second.compiled.has_value()) {
                    continue;
                }
                if (candidate == entries_.end() ||
                    current->second.last_used_tick <
                        candidate->second.last_used_tick) {
                    candidate = current;
                }
            }
            if (candidate == entries_.end()) return false;
            release_compiled_entry(candidate->second);
            candidate->second.observed_calls = 0U;
            ++stats_.evicted_methods;
        }
        return true;
    }

    std::unordered_map<MethodId, Entry, MetadataIdHash<MethodId>> entries_;
#endif
    bool enabled_ {true};
    JitAvailability availability_ {JitAvailability::unavailable};
    u32 hot_threshold_ {kDefaultHotThreshold};
    u32 unavailable_probe_countdown_ {0U};
    u64 code_cache_limit_bytes_ {kDefaultCodeCacheBytes};
    u64 code_cache_bytes_ {0U};
    u64 use_tick_ {0U};
    JitStatistics stats_;
};

BaselineJit::BaselineJit(bool enabled)
    : impl_(std::make_unique<Impl>(enabled)) {}

BaselineJit::~BaselineJit() = default;

void BaselineJit::set_enabled(bool enabled) noexcept {
    impl_->set_enabled(enabled);
}

void BaselineJit::refresh_availability() noexcept {
    impl_->refresh_availability();
}

void BaselineJit::clear_cache() noexcept { impl_->clear_cache(); }

bool BaselineJit::enabled() const noexcept { return impl_->enabled(); }

JitAvailability BaselineJit::availability() const noexcept {
    return impl_->availability();
}

JitStatistics BaselineJit::statistics() const noexcept {
    return impl_->statistics();
}

Result<std::optional<JitExecutionResult>> BaselineJit::try_execute(
    MethodId method_id,
    const classfile::ClassFile& owner,
    const classfile::Method& method,
    const CachedMethodDescriptor& descriptor,
    std::span<const Value> arguments,
    bool has_receiver,
    u64 instruction_budget) {
    return impl_->try_execute(method_id,
                              owner,
                              method,
                              descriptor,
                              arguments,
                              has_receiver,
                              instruction_budget);
}

JitAvailability BaselineJit::probe_platform() noexcept {
    return Impl::probe_platform();
}

} // namespace phoneme::vm
