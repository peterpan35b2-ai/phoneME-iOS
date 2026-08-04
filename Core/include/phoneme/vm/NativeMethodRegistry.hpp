#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;

using NativeMethod = std::function<Result<std::optional<Value>>(
    Machine& machine,
    std::span<const Value> arguments)>;

struct NativeMethodSignature final {
    std::string owner;
    std::string name;
    std::string descriptor;
};

struct NativeMethodInvocationCount final {
    NativeMethodSignature signature;
    std::size_t count {0};
};

class NativeMethodRegistry final {
public:
    [[nodiscard]] Status register_method(std::string owner,
                                         std::string name,
                                         std::string descriptor,
                                         NativeMethod implementation);
    [[nodiscard]] bool contains(std::string_view owner,
                                std::string_view name,
                                std::string_view descriptor) const noexcept;
    [[nodiscard]] Result<std::optional<Value>> invoke(
        Machine& machine,
        std::string_view owner,
        std::string_view name,
        std::string_view descriptor,
        std::span<const Value> arguments) const;
    [[nodiscard]] std::vector<NativeMethodSignature>
    registered_methods() const;
    [[nodiscard]] std::vector<NativeMethodInvocationCount>
    invocation_counts() const;
    void reset_invocation_counts() noexcept;
    void clear() noexcept;

private:
    [[nodiscard]] static std::string key(std::string_view owner,
                                         std::string_view name,
                                         std::string_view descriptor);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, NativeMethod> methods_;
    std::unordered_map<std::string, NativeMethodSignature> signatures_;
    mutable std::unordered_map<std::string, std::size_t> invocation_counts_;
};

void register_core_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
