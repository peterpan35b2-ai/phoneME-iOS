#pragma once

#include <memory>
#include <optional>
#include <span>

#include "phoneme/base/Error.hpp"
#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/MetadataId.hpp"
#include "phoneme/vm/RuntimeMetadata.hpp"
#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

enum class JitAvailability : i32 {
    unavailable = 0,
    ready = 1,
};

struct JitExecutionResult final {
    std::optional<Value> return_value;
    u32 bytecode_instructions {0};
};

struct JitStatistics final {
    u64 compiled_methods {0};
    u64 executed_methods {0};
    u64 rejected_methods {0};
    u64 deoptimized_executions {0};
    u64 evicted_methods {0};
    u64 optimized_methods {0};
    u64 loop_optimized_methods {0};
    u64 propagated_constants {0};
    u64 folded_operations {0};
    u64 folded_branches {0};
    u64 strength_reductions {0};
    u64 budget_checks_elided {0};
    u64 code_cache_bytes {0};
    u64 code_cache_limit_bytes {0};
};

// ARM64 optimizing JIT for verified, side-effect-free integer methods. It
// supports integer locals, constants, arithmetic, branches, loops and switch
// bytecodes, with basic-block constant propagation, branch/switch folding,
// strength reduction and adaptive hot-loop compilation. Every generated path
// remains guarded by the VM instruction budget. Unsupported object/heap/call/
// exception semantics fall back to the interpreter without changing behavior.
class BaselineJit final {
public:
    explicit BaselineJit(bool enabled = true);
    ~BaselineJit();

    BaselineJit(const BaselineJit&) = delete;
    BaselineJit& operator=(const BaselineJit&) = delete;

    void set_enabled(bool enabled) noexcept;
    void refresh_availability() noexcept;
    void clear_cache() noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] JitAvailability availability() const noexcept;
    [[nodiscard]] JitStatistics statistics() const noexcept;

    [[nodiscard]] Result<std::optional<JitExecutionResult>> try_execute(
        MethodId method_id,
        const classfile::ClassFile& owner,
        const classfile::Method& method,
        const CachedMethodDescriptor& descriptor,
        std::span<const Value> arguments,
        bool has_receiver,
        u64 instruction_budget);

    [[nodiscard]] static JitAvailability probe_platform() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace phoneme::vm
