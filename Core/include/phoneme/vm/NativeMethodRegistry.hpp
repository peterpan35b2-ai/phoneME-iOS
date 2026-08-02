#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;

using NativeMethod = std::function<Result<std::optional<Value>>(
    Machine& machine,
    std::span<const Value> arguments)>;

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
    void clear() noexcept;

private:
    [[nodiscard]] static std::string key(std::string_view owner,
                                         std::string_view name,
                                         std::string_view descriptor);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, NativeMethod> methods_;
};

void register_core_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
