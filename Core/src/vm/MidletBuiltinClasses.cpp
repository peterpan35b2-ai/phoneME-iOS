#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_midlet_class(std::string_view name) {
    if (name == "javax/microedition/midlet/MIDlet") {
        return make_class("javax/microedition/midlet/MIDlet",
                          "java/lang/Object", kOrdinary | kAbstract, {}, {
            method(kProtected, "<init>", "()V"),
            method(kProtected | kAbstract, "startApp", "()V"),
            method(kProtected | kAbstract, "pauseApp", "()V"),
            method(kProtected | kAbstract, "destroyApp", "(Z)V"),
            method(kPublic | kFinal, "notifyDestroyed", "()V"),
            method(kPublic | kFinal, "notifyPaused", "()V"),
            method(kPublic | kFinal, "resumeRequest", "()V"),
            method(kPublic | kFinal, "getAppProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic | kFinal, "checkPermission",
                   "(Ljava/lang/String;)I"),
            method(kPublic | kFinal, "platformRequest", "(Ljava/lang/String;)Z"),
        });
    }
    if (name == "javax/microedition/midlet/MIDletStateChangeException") {
        return make_class("javax/microedition/midlet/MIDletStateChangeException",
                          "java/lang/Exception", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    return nullptr;
}

} // namespace

void register_midlet_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_midlet_class);
}

} // namespace phoneme::vm
