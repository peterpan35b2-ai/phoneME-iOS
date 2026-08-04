#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include <string_view>

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] BuiltinClassRegistry::ClassPtr build_amms_class(
    std::string_view name) {
    const u16 api = kPublic | kAbstract;

    if (name == "javax/microedition/amms/control/EffectControl") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kPublic | kStatic | kFinal, "SCOPE_LIVE_ONLY", "I"),
            field(kPublic | kStatic | kFinal, "SCOPE_RECORD_ONLY", "I"),
            field(kPublic | kStatic | kFinal, "SCOPE_LIVE_AND_RECORD", "I"),
        }, {
            method(api, "getPreset", "()Ljava/lang/String;"),
            method(api, "getPresetNames", "()[Ljava/lang/String;"),
            method(api, "setPreset", "(Ljava/lang/String;)V"),
            method(api, "isEnabled", "()Z"),
            method(api, "setEnabled", "(Z)V"),
            method(api, "isEnforced", "()Z"),
            method(api, "setEnforced", "(Z)V"),
            method(api, "getScope", "()I"),
            method(api, "setScope", "(I)V"),
        }, {"javax/microedition/media/Control"});
    }
    if (name == "javax/microedition/amms/control/audioeffect/EqualizerControl") {
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {}, {
            method(api, "getBand", "(I)I"),
            method(api, "getBandLevel", "(I)I"),
            method(api, "getCenterFreq", "(I)I"),
            method(api, "getMaxBandLevel", "()I"),
            method(api, "getMinBandLevel", "()I"),
            method(api, "getNumberOfBands", "()I"),
            method(api, "setBandLevel", "(II)V"),
            method(api, "setBass", "(I)V"),
            method(api, "setTreble", "(I)V"),
        }, {"javax/microedition/amms/control/EffectControl"});
    }
    if (name == "javax/microedition/amms/GlobalManager") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "getControl",
                   "(Ljava/lang/String;)Ljavax/microedition/media/Control;"),
            method(kPublic | kStatic, "getControls",
                   "()[Ljavax/microedition/media/Control;"),
        });
    }
    if (name == "phoneme/amms/EqualizerControlImpl") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate, "levels", "[I"),
            field(kPrivate, "enabled", "I"),
            field(kPrivate, "enforced", "I"),
            field(kPrivate, "scope", "I"),
            field(kPrivate, "preset", "Ljava/lang/String;"),
        }, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "getPreset", "()Ljava/lang/String;"),
            method(kPublic, "getPresetNames", "()[Ljava/lang/String;"),
            method(kPublic, "setPreset", "(Ljava/lang/String;)V"),
            method(kPublic, "isEnabled", "()Z"),
            method(kPublic, "setEnabled", "(Z)V"),
            method(kPublic, "isEnforced", "()Z"),
            method(kPublic, "setEnforced", "(Z)V"),
            method(kPublic, "getScope", "()I"),
            method(kPublic, "setScope", "(I)V"),
            method(kPublic, "getBand", "(I)I"),
            method(kPublic, "getBandLevel", "(I)I"),
            method(kPublic, "getCenterFreq", "(I)I"),
            method(kPublic, "getMaxBandLevel", "()I"),
            method(kPublic, "getMinBandLevel", "()I"),
            method(kPublic, "getNumberOfBands", "()I"),
            method(kPublic, "setBandLevel", "(II)V"),
            method(kPublic, "setBass", "(I)V"),
            method(kPublic, "setTreble", "(I)V"),
        }, {"javax/microedition/amms/control/audioeffect/EqualizerControl"});
    }
    return nullptr;
}

} // namespace

void register_amms_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_amms_class);
}

} // namespace phoneme::vm
