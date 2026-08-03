#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

struct ExecutionResult final {
    std::optional<Value> return_value;
    std::optional<ObjectRef> throwable;
    u64 executed_instructions {0};
    std::string exception_context;

    [[nodiscard]] bool completed_normally() const noexcept {
        return !throwable.has_value();
    }
};

class Interpreter final {
public:
    [[nodiscard]] Result<ExecutionResult> execute(
        const classfile::CodeAttribute& code,
        std::span<const Value> initial_locals = {},
        u64 instruction_budget = 10'000'000) const;
};

} // namespace phoneme::vm
