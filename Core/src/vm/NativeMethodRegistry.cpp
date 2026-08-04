#include "phoneme/vm/NativeMethodRegistry.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

#include "phoneme/vm/Machine.hpp"

namespace phoneme::vm {

Status NativeMethodRegistry::register_method(
    std::string owner,
    std::string name,
    std::string descriptor,
    NativeMethod implementation) {
    if (owner.empty() || name.empty() || descriptor.empty() || !implementation) {
        return fail(ErrorCode::invalid_argument,
                    "native method registration is incomplete");
    }

    const std::string method_key = key(owner, name, descriptor);
    std::scoped_lock lock(mutex_);
    if (methods_.contains(method_key)) {
        return fail(ErrorCode::invalid_state,
                    "native method is already registered");
    }
    methods_.emplace(method_key, std::move(implementation));
    signatures_.emplace(method_key, NativeMethodSignature {
        .owner = std::move(owner),
        .name = std::move(name),
        .descriptor = std::move(descriptor),
    });
    invocation_counts_.emplace(method_key, 0U);
    return {};
}

bool NativeMethodRegistry::contains(std::string_view owner,
                                    std::string_view name,
                                    std::string_view descriptor) const noexcept {
    std::scoped_lock lock(mutex_);
    return methods_.contains(key(owner, name, descriptor));
}

Result<std::optional<Value>> NativeMethodRegistry::invoke(
    Machine& machine,
    std::string_view owner,
    std::string_view name,
    std::string_view descriptor,
    std::span<const Value> arguments) const {
    NativeMethod implementation;
    {
        std::scoped_lock lock(mutex_);
        const auto iterator = methods_.find(key(owner, name, descriptor));
        if (iterator == methods_.end()) {
            return fail(ErrorCode::unsupported_feature,
                        "native method is not ported: " + std::string(owner) +
                            "." + std::string(name) +
                            std::string(descriptor));
        }
        implementation = iterator->second;
        auto count = invocation_counts_.find(key(owner, name, descriptor));
        if (count != invocation_counts_.end()) {
            ++count->second;
        }
    }
    auto result = implementation(machine, arguments);
    if (!result) {
        Error error = result.error();
        error.message = std::string(owner) + "." + std::string(name) +
                        std::string(descriptor) + ": " + error.message;
        return std::unexpected(std::move(error));
    }
    return result;
}

std::vector<NativeMethodSignature>
NativeMethodRegistry::registered_methods() const {
    std::scoped_lock lock(mutex_);
    std::vector<NativeMethodSignature> result;
    result.reserve(signatures_.size());
    for (const auto& [method_key, signature] : signatures_) {
        static_cast<void>(method_key);
        result.push_back(signature);
    }
    std::ranges::sort(result, {}, [](const NativeMethodSignature& signature) {
        return std::tie(signature.owner, signature.name, signature.descriptor);
    });
    return result;
}

std::vector<NativeMethodInvocationCount>
NativeMethodRegistry::invocation_counts() const {
    std::scoped_lock lock(mutex_);
    std::vector<NativeMethodInvocationCount> result;
    result.reserve(signatures_.size());
    for (const auto& [method_key, signature] : signatures_) {
        const auto count = invocation_counts_.find(method_key);
        result.push_back(NativeMethodInvocationCount {
            .signature = signature,
            .count = count == invocation_counts_.end() ? 0U : count->second,
        });
    }
    std::ranges::sort(result, {},
        [](const NativeMethodInvocationCount& entry) {
            return std::tie(entry.signature.owner,
                            entry.signature.name,
                            entry.signature.descriptor);
        });
    return result;
}

void NativeMethodRegistry::reset_invocation_counts() noexcept {
    std::scoped_lock lock(mutex_);
    for (auto& [method_key, count] : invocation_counts_) {
        static_cast<void>(method_key);
        count = 0U;
    }
}

void NativeMethodRegistry::clear() noexcept {
    std::scoped_lock lock(mutex_);
    methods_.clear();
    signatures_.clear();
    invocation_counts_.clear();
}

std::string NativeMethodRegistry::key(std::string_view owner,
                                      std::string_view name,
                                      std::string_view descriptor) {
    std::string result;
    result.reserve(owner.size() + name.size() + descriptor.size() + 2);
    result.append(owner);
    result.push_back('#');
    result.append(name);
    result.push_back(':');
    result.append(descriptor);
    return result;
}

} // namespace phoneme::vm
