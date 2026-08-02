#include "SecurityNatives.hpp"

#include <exception>
#include <span>
#include <string>
#include <utility>

#include "phoneme/security/PermissionPolicy.hpp"
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
                    "security method has no receiver");
    }
    auto receiver = arguments.front().as_reference();
    if (!receiver) {
        return std::unexpected(receiver.error());
    }
    if (receiver->is_null()) {
        return fail_java("java/lang/NullPointerException",
                         "security receiver is null");
    }
    return *receiver;
}

[[nodiscard]] Result<std::string> utf8_string(Machine& machine,
                                              ObjectRef reference,
                                              bool allow_null) {
    if (reference.is_null()) {
        if (allow_null) {
            return std::string {};
        }
        return fail_java("java/lang/NullPointerException",
                         "permission name is null");
    }
    auto text = machine.heap().string_value(reference);
    if (!text) {
        return std::unexpected(text.error());
    }

    std::string result;
    result.reserve(text->size());
    for (usize index = 0; index < text->size(); ++index) {
        u32 code_point = static_cast<u16>((*text)[index]);
        if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
            if (index + 1U < text->size()) {
                const u32 low = static_cast<u16>((*text)[index + 1U]);
                if (low >= 0xDC00U && low <= 0xDFFFU) {
                    code_point = 0x10000U +
                        ((code_point - 0xD800U) << 10U) +
                        (low - 0xDC00U);
                    ++index;
                } else {
                    code_point = 0xFFFDU;
                }
            } else {
                code_point = 0xFFFDU;
            }
        } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
            code_point = 0xFFFDU;
        }
        if (code_point <= 0x7FU) {
            result.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FFU) {
            result.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else if (code_point <= 0xFFFFU) {
            result.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            result.push_back(static_cast<char>(0x80U |
                                               ((code_point >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else {
            result.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
            result.push_back(static_cast<char>(0x80U |
                                               ((code_point >> 12U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U |
                                               ((code_point >> 6U) & 0x3FU)));
            result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
    }
    return result;
}

[[nodiscard]] Result<std::string> string_argument(
    Machine& machine,
    std::span<const Value> arguments,
    usize index,
    bool allow_null) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "security method is missing a string argument");
    }
    auto reference = arguments[index].as_reference();
    if (!reference) {
        return std::unexpected(reference.error());
    }
    return utf8_string(machine, *reference, allow_null);
}

[[nodiscard]] i32 java_decision(
    security::PermissionDecision decision) noexcept {
    return static_cast<i32>(to_underlying(decision));
}

} // namespace

void register_security_natives(NativeMethodRegistry& registry) {
    add(registry,
        "javax/microedition/midlet/MIDlet",
        "checkPermission",
        "(Ljava/lang/String;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto receiver = require_receiver(arguments);
            if (!receiver) {
                return std::unexpected(receiver.error());
            }
            if (arguments.size() != 2U) {
                return fail(ErrorCode::invalid_argument,
                            "MIDlet.checkPermission expects one argument");
            }
            auto permission = string_argument(machine, arguments, 1U, false);
            if (!permission) {
                return std::unexpected(permission.error());
            }
            return std::optional<Value>(Value::from_int(java_decision(
                machine.permission_policy().check(*permission))));
        });

    add(registry,
        "com/sun/midp/security/PermissionGate",
        "checkPermission",
        "(Ljava/lang/String;)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 1U) {
                return fail(ErrorCode::invalid_argument,
                            "PermissionGate.checkPermission expects one argument");
            }
            auto permission = string_argument(machine, arguments, 0U, false);
            if (!permission) {
                return std::unexpected(permission.error());
            }
            return std::optional<Value>(Value::from_int(java_decision(
                machine.permission_policy().check(*permission))));
        });

    add(registry,
        "com/sun/midp/security/PermissionGate",
        "requestPermission",
        "(Ljava/lang/String;Ljava/lang/String;Z)I",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "PermissionGate.requestPermission expects three arguments");
            }
            auto permission = string_argument(machine, arguments, 0U, false);
            auto resource = string_argument(machine, arguments, 1U, true);
            auto user_initiated = arguments[2].as_int();
            if (!permission) return std::unexpected(permission.error());
            if (!resource) return std::unexpected(resource.error());
            if (!user_initiated) {
                return std::unexpected(user_initiated.error());
            }
            auto response = machine.permission_policy().request(
                *permission, std::move(*resource), *user_initiated != 0);
            if (!response) {
                return std::unexpected(response.error());
            }
            return std::optional<Value>(Value::from_int(
                java_decision(response->decision)));
        });

    add(registry,
        "com/sun/midp/security/PermissionGate",
        "requirePermission",
        "(Ljava/lang/String;Ljava/lang/String;Z)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            if (arguments.size() != 3U) {
                return fail(ErrorCode::invalid_argument,
                            "PermissionGate.requirePermission expects three arguments");
            }
            auto permission = string_argument(machine, arguments, 0U, false);
            auto resource = string_argument(machine, arguments, 1U, true);
            auto user_initiated = arguments[2].as_int();
            if (!permission) return std::unexpected(permission.error());
            if (!resource) return std::unexpected(resource.error());
            if (!user_initiated) {
                return std::unexpected(user_initiated.error());
            }
            auto allowed = machine.permission_policy().require(
                *permission, std::move(*resource), *user_initiated != 0);
            if (!allowed) {
                return std::unexpected(allowed.error());
            }
            return std::optional<Value> {};
        });
}

} // namespace phoneme::vm
