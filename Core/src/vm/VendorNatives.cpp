#include "VendorNatives.hpp"

#include <exception>
#include <span>
#include <string>
#include <utility>

#include "phoneme/vm/Machine.hpp"
#include "phoneme/vm/NativeMethodRegistry.hpp"

namespace phoneme::vm {
namespace {

constexpr std::string_view kSprintSystem = "com/sprintpcs/util/System";

void add(NativeMethodRegistry& registry,
         std::string owner,
         std::string name,
         std::string descriptor,
         NativeMethod method) {
    auto registered = registry.register_method(std::move(owner),
                                               std::move(name),
                                               std::move(descriptor),
                                               std::move(method));
    if (!registered) std::terminate();
}

[[nodiscard]] Result<ObjectRef> reference_argument(
    std::span<const Value> arguments,
    usize index,
    bool allow_null = true) {
    if (index >= arguments.size()) {
        return fail(ErrorCode::invalid_argument,
                    "SprintPCS native reference argument is missing");
    }
    auto value = arguments[index].as_reference();
    if (!value) return std::unexpected(value.error());
    if (!allow_null && value->is_null()) {
        return fail_java("java/lang/IllegalArgumentException",
                         "SprintPCS argument must not be null");
    }
    return *value;
}

[[nodiscard]] Result<std::optional<Value>> system_property(
    Machine& machine,
    ObjectRef key) {
    const Value argument = Value::from_reference(key);
    auto result = machine.invoke_static("java/lang/System", "getProperty",
                                        "(Ljava/lang/String;)Ljava/lang/String;",
                                        std::span<const Value>(&argument, 1U));
    if (!result) return std::unexpected(result.error());
    if (result->throwable.has_value()) {
        auto type = machine.heap().class_name(*result->throwable);
        if (!type) return std::unexpected(type.error());
        return fail_java(*type, "SprintPCS property lookup failed");
    }
    return result->return_value;
}

} // namespace

void register_vendor_natives(NativeMethodRegistry& registry) {
    add(registry, std::string(kSprintSystem), "<init>", "()V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto object = reference_argument(arguments, 0U, false);
            if (!object) return std::unexpected(object.error());
            return std::optional<Value> {};
        });

    add(registry, std::string(kSprintSystem), "addSystemListener",
        "(Lcom/sprintpcs/util/SystemEventListener;)V",
        [](Machine&, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto listener = reference_argument(arguments, 0U);
            if (!listener) return std::unexpected(listener.error());
            // iOS has no Sprint handset system-event bus. Treat registration as
            // accepted so legacy applications can continue without depending
            // on the emulator-only com.sun.kvem implementation.
            return std::optional<Value> {};
        });

    for (const std::string_view method : {"getProtectedProperty",
                                          "getSystemState"}) {
        add(registry, std::string(kSprintSystem), std::string(method),
            "(Ljava/lang/String;)Ljava/lang/String;",
            [](Machine& machine, std::span<const Value> arguments)
                -> Result<std::optional<Value>> {
                auto key = reference_argument(arguments, 0U, false);
                if (!key) return std::unexpected(key.error());
                return system_property(machine, *key);
            });
    }

    add(registry, std::string(kSprintSystem), "promptMasterVolume", "()V",
        [](Machine&, std::span<const Value>)
            -> Result<std::optional<Value>> {
            // Volume is controlled by the native media/UI bridge. The Sprint
            // prompt is advisory, so a stable no-op is preferable to failing.
            return std::optional<Value> {};
        });

    add(registry, std::string(kSprintSystem), "setExitURI",
        "(Ljava/lang/String;)V",
        [](Machine& machine, std::span<const Value> arguments)
            -> Result<std::optional<Value>> {
            auto uri = reference_argument(arguments, 0U, false);
            if (!uri) return std::unexpected(uri.error());
            auto text = machine.heap().string_value(*uri);
            if (!text) return std::unexpected(text.error());
            if (text->empty()) {
                return fail_java("java/lang/IllegalArgumentException",
                                 "SprintPCS exit URI is empty");
            }
            // Preserve the request for the iOS host/lifecycle layer. Legacy
            // games commonly call this immediately before notifyDestroyed().
            machine.set_system_property(u"com.sprintpcs.exitURI",
                                        std::move(*text));
            return std::optional<Value> {};
        });
}

} // namespace phoneme::vm
