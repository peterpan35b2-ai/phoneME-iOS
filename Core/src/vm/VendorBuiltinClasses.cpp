#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] BuiltinClassRegistry::ClassPtr build_vendor_class(
    std::string_view name) {
    if (name == "com/sprintpcs/util/SystemEventListener") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(kPublic | kAbstract, "systemEvent",
                   "(Ljava/lang/String;Ljava/lang/String;)V"),
        });
    }
    if (name == "com/sprintpcs/util/System") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic | kStatic, "addSystemListener",
                   "(Lcom/sprintpcs/util/SystemEventListener;)V"),
            method(kPublic | kStatic, "getProtectedProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic | kStatic, "getSystemState",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic | kStatic, "promptMasterVolume", "()V"),
            method(kPublic | kStatic, "setExitURI", "(Ljava/lang/String;)V"),
        });
    }
    return nullptr;
}

} // namespace

void register_vendor_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_vendor_class);
}

} // namespace phoneme::vm
