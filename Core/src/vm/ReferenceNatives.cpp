#include "ReferenceNatives.hpp"

#include <exception>
#include <span>
#include <string>
#include <utility>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod method) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(method));
    if (!registered) {
        std::terminate();
    }
}

[[nodiscard]] Result<ObjectRef> require_receiver(
    std::span<const Value> arguments) {
    if (arguments.empty()) {
        return fail(ErrorCode::invalid_argument,
                    "reference native is missing its receiver");
    }
    auto receiver = arguments.front().as_reference();
    if (!receiver || receiver->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "reference native receiver is null");
    }
    return *receiver;
}

[[nodiscard]] Result<std::optional<Value>> initialize_reference(
    Machine& machine,
    std::span<const Value> arguments) {
    if (arguments.size() != 2U) {
        return fail(ErrorCode::invalid_argument,
                    "Reference constructor expects one argument");
    }
    auto receiver = require_receiver(arguments);
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    auto referent = arguments[1].as_reference();
    if (!referent) {
        return std::unexpected(referent.error());
    }
    auto stored = machine.heap().set_weak_referent(*receiver, *referent);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    return std::optional<Value> {};
}

[[nodiscard]] Result<std::optional<Value>> get_reference(
    Machine& machine,
    std::span<const Value> arguments) {
    if (arguments.size() != 1U) {
        return fail(ErrorCode::invalid_argument,
                    "Reference.get expects no arguments");
    }
    auto receiver = require_receiver(arguments);
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    auto referent = machine.heap().weak_referent(*receiver);
    if (!referent) {
        return std::unexpected(referent.error());
    }
    return std::optional<Value>(Value::from_reference(*referent));
}

[[nodiscard]] Result<std::optional<Value>> clear_reference(
    Machine& machine,
    std::span<const Value> arguments) {
    if (arguments.size() != 1U) {
        return fail(ErrorCode::invalid_argument,
                    "Reference.clear expects no arguments");
    }
    auto receiver = require_receiver(arguments);
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    auto cleared = machine.heap().clear_weak_referent(*receiver);
    if (!cleared) {
        return std::unexpected(cleared.error());
    }
    return std::optional<Value> {};
}

void register_reference_class(NativeMethodRegistry& registry,
                              std::string owner) {
    add(registry, owner, "<init>", "(Ljava/lang/Object;)V",
        initialize_reference);
    add(registry, owner, "get", "()Ljava/lang/Object;", get_reference);
    add(registry, owner, "clear", "()V", clear_reference);
}

} // namespace

void register_reference_natives(NativeMethodRegistry& registry) {
    register_reference_class(registry, "java/lang/ref/Reference");
    register_reference_class(registry, "java/lang/ref/WeakReference");
}

} // namespace phoneme::vm
