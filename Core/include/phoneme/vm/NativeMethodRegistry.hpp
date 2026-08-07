#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "phoneme/vm/MetadataId.hpp"
#include "phoneme/vm/Value.hpp"

namespace phoneme::vm {

class Machine;

using NativeMethod = std::function<Result<std::optional<Value>>(
    Machine& machine,
    std::span<const Value> arguments)>;

enum class NativeJitPolicy : u8 {
    conservative,
    synchronous_bounded,
};

struct NativeMethodSignature final {
    NativeMethodId id;
    std::string owner;
    std::string name;
    std::string descriptor;
};

struct NativeMethodInvocationCount final {
    NativeMethodSignature signature;
    std::size_t count {0};
};

struct NativeMethodBinding final {
    NativeMethodId id;
    u64 generation {0U};
};

class NativeMethodRegistry final {
public:
    [[nodiscard]] Status register_method(
        std::string owner,
        std::string name,
        std::string descriptor,
        NativeMethod implementation,
        NativeJitPolicy jit_policy = NativeJitPolicy::conservative);
    [[nodiscard]] Status register_alias(std::string_view source_owner,
                                        std::string_view source_name,
                                        std::string_view source_descriptor,
                                        std::string target_owner,
                                        std::string target_name,
                                        std::string target_descriptor);
    [[nodiscard]] NativeMethodId resolve(std::string_view owner,
                                         std::string_view name,
                                         std::string_view descriptor) const noexcept;
    [[nodiscard]] NativeMethodBinding resolve_binding(
        std::string_view owner,
        std::string_view name,
        std::string_view descriptor) const noexcept;
    [[nodiscard]] bool contains(std::string_view owner,
                                std::string_view name,
                                std::string_view descriptor) const noexcept;
    [[nodiscard]] NativeJitPolicy jit_policy(
        NativeMethodId method_id) const noexcept;
    [[nodiscard]] Result<std::optional<Value>> invoke(
        Machine& machine,
        NativeMethodId method_id,
        std::span<const Value> arguments) const;
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
    [[nodiscard]] u64 generation() const noexcept;
    void clear() noexcept;

private:
    struct Entry final {
        NativeMethodSignature signature;
        NativeMethod implementation;
        NativeJitPolicy jit_policy {NativeJitPolicy::conservative};
        std::size_t invocation_count {0};
    };

    [[nodiscard]] static std::string key(std::string_view owner,
                                         std::string_view name,
                                         std::string_view descriptor);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, NativeMethodId> ids_by_key_;
    mutable std::vector<Entry> entries_;
    u64 generation_ {1U};
};

void register_core_natives(NativeMethodRegistry& registry);

} // namespace phoneme::vm
