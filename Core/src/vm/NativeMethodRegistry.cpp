#include "phoneme/vm/NativeMethodRegistry.hpp"

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

void NativeMethodRegistry::clear() noexcept {
    std::scoped_lock lock(mutex_);
    methods_.clear();
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
