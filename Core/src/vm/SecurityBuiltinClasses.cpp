#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_security_class(std::string_view name) {
    if (name == "com/sun/midp/security/PermissionGate") {
        return make_class("com/sun/midp/security/PermissionGate",
                          "java/lang/Object", kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "checkPermission",
                   "(Ljava/lang/String;)I"),
            method(kPublic | kStatic, "requestPermission",
                   "(Ljava/lang/String;Ljava/lang/String;Z)I"),
            method(kPublic | kStatic, "requirePermission",
                   "(Ljava/lang/String;Ljava/lang/String;Z)V"),
        });
    }
    return nullptr;
}

} // namespace

void register_security_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_security_class);
}

} // namespace phoneme::vm
