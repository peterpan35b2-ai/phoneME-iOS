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
    if (name == "com/samsung/util/AudioClip") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate, "type", "I"),
            field(kPrivate, "resource", "Ljava/lang/String;"),
            field(kPrivate, "data", "[B"),
            field(kPrivate, "player", "Ljavax/microedition/media/Player;"),
        }, {
            method(kPublic, "<init>", "(ILjava/lang/String;)V"),
            method(kPublic, "<init>", "(I[BII)V"),
            method(kPublic, "play", "(II)V"),
            method(kPublic, "stop", "()V"),
            method(kPublic, "pause", "()V"),
            method(kPublic, "resume", "()V"),
        });
    }
    if (name == "com/sprintpcs/media/Clip") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate, "resource", "Ljava/lang/String;"),
            field(kPrivate, "contentType", "Ljava/lang/String;"),
            field(kPrivate, "priority", "I"),
            field(kPrivate, "vibration", "I"),
            field(kPrivate, "player", "Ljavax/microedition/media/Player;"),
        }, {
            method(kPublic, "<init>", "(Ljava/lang/String;Ljava/lang/String;II)V"),
            method(kPublic, "<init>", "([BLjava/lang/String;II)V"),
            method(kPublic, "getPriority", "()I"),
            method(kPublic, "getVibration", "()I"),
            method(kPublic, "getPlayer", "()Ljavax/microedition/media/Player;"),
        });
    }
    if (name == "com/sprintpcs/media/Player") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {
            field(kPrivate | kStatic, "foregroundPlayer",
                  "Ljavax/microedition/media/Player;"),
            field(kPrivate | kStatic, "backgroundPlayer",
                  "Ljavax/microedition/media/Player;"),
        }, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "play", "(Lcom/sprintpcs/media/Clip;I)V"),
            method(kPublic | kStatic, "playBackground", "(Lcom/sprintpcs/media/Clip;I)V"),
            method(kPublic | kStatic, "stop", "()V"),
        });
    }
    if (name == "com/siemens/mp/gsm/SMS") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "send", "(Ljava/lang/String;Ljava/lang/String;)I"),
        });
    }
    if (name == "com/vodafone/util/ImageEncoder") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {
            field(kPrivate, "imageType", "I"),
        }, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "createEncoder",
                   "(I)Lcom/vodafone/util/ImageEncoder;"),
            method(kPublic, "encodeOffscreen",
                   "(Ljavax/microedition/lcdui/Image;IIII)[B"),
        });
    }
    if (name == "com/sun/midp/midlet/MIDletSuite") {
        constexpr u16 kConstant = kPublic | kStatic | kFinal;
        constexpr u16 kInterfaceMethod = kPublic | kAbstract;
        return make_class(std::string(name), "java/lang/Object",
                          kPublic | kInterface | kAbstract, {
            field(kConstant, "UNUSED_SUITE_ID", "I"),
            field(kConstant, "INTERNAL_SUITE_ID", "I"),
            field(kConstant, "JAR_MANIFEST", "Ljava/lang/String;"),
            field(kConstant, "DATA_SIZE_PROP", "Ljava/lang/String;"),
            field(kConstant, "JAR_SIZE_PROP", "Ljava/lang/String;"),
            field(kConstant, "JAR_URL_PROP", "Ljava/lang/String;"),
            field(kConstant, "SUITE_NAME_PROP", "Ljava/lang/String;"),
            field(kConstant, "VENDOR_PROP", "Ljava/lang/String;"),
            field(kConstant, "VERSION_PROP", "Ljava/lang/String;"),
            field(kConstant, "DESC_PROP", "Ljava/lang/String;"),
            field(kConstant, "CONFIGURATION_PROP", "Ljava/lang/String;"),
            field(kConstant, "PROFILE_PROP", "Ljava/lang/String;"),
            field(kConstant, "RUNTIME_EXEC_ENV_PROP", "Ljava/lang/String;"),
            field(kConstant, "RUNTIME_EXEC_ENV_DEFAULT", "Ljava/lang/String;"),
            field(kConstant, "PERMISSIONS_PROP", "Ljava/lang/String;"),
            field(kConstant, "PERMISSIONS_OPT_PROP", "Ljava/lang/String;"),
            field(kConstant, "HEAP_SIZE_PROP", "Ljava/lang/String;"),
            field(kConstant, "BACKGROUND_PAUSE_PROP", "Ljava/lang/String;"),
            field(kConstant, "NO_EXIT_PROP", "Ljava/lang/String;"),
            field(kConstant, "LAUNCH_BG_PROP", "Ljava/lang/String;"),
            field(kConstant, "LAUNCH_POWER_ON_PROP", "Ljava/lang/String;"),
        }, {
            method(kInterfaceMethod, "getProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kInterfaceMethod, "getPushInterruptSetting", "()B"),
            method(kInterfaceMethod, "getPushOptions", "()I"),
            method(kInterfaceMethod, "getPermissions", "()[B"),
            method(kInterfaceMethod, "setTempProperty",
                   "(Lcom/sun/midp/security/SecurityToken;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kInterfaceMethod, "getMIDletName",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kInterfaceMethod, "checkIfPermissionAllowed",
                   "(Ljava/lang/String;)V"),
            method(kInterfaceMethod, "checkForPermission",
                   "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kInterfaceMethod, "checkForPermission",
                   "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kInterfaceMethod, "checkPermission",
                   "(Ljava/lang/String;)I"),
            method(kInterfaceMethod, "getID", "()I"),
            method(kInterfaceMethod, "getMIDletNumber",
                   "(Ljava/lang/String;)I"),
            method(kInterfaceMethod, "permissionToInterrupt",
                   "(Ljava/lang/String;)Z"),
            method(kInterfaceMethod, "isRegistered",
                   "(Ljava/lang/String;)Z"),
            method(kInterfaceMethod, "isTrusted", "()Z"),
            method(kInterfaceMethod, "isVerified", "()Z"),
            method(kInterfaceMethod, "isEnabled", "()Z"),
            method(kInterfaceMethod, "getSecureFilenameBase",
                   "()Ljava/lang/String;"),
            method(kInterfaceMethod, "close", "()V"),
        });
    }
    if (name == "com/sun/midp/midlet/CurrentMIDletSuite") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "getProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "getPushInterruptSetting", "()B"),
            method(kPublic, "getPushOptions", "()I"),
            method(kPublic, "getPermissions", "()[B"),
            method(kPublic, "setTempProperty",
                   "(Lcom/sun/midp/security/SecurityToken;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "getMIDletName",
                   "(Ljava/lang/String;)Ljava/lang/String;"),
            method(kPublic, "checkIfPermissionAllowed",
                   "(Ljava/lang/String;)V"),
            method(kPublic, "checkForPermission",
                   "(Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "checkForPermission",
                   "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V"),
            method(kPublic, "checkPermission", "(Ljava/lang/String;)I"),
            method(kPublic, "getID", "()I"),
            method(kPublic, "getMIDletNumber", "(Ljava/lang/String;)I"),
            method(kPublic, "permissionToInterrupt", "(Ljava/lang/String;)Z"),
            method(kPublic, "isRegistered", "(Ljava/lang/String;)Z"),
            method(kPublic, "isTrusted", "()Z"),
            method(kPublic, "isVerified", "()Z"),
            method(kPublic, "isEnabled", "()Z"),
            method(kPublic, "getSecureFilenameBase", "()Ljava/lang/String;"),
            method(kPublic, "close", "()V"),
        }, {"com/sun/midp/midlet/MIDletSuite"});
    }
    if (name == "com/sun/midp/midlet/Scheduler") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary, {
            field(kPrivate | kStatic, "scheduler",
                  "Lcom/sun/midp/midlet/Scheduler;"),
            field(kPrivate | kStatic, "currentSuite",
                  "Lcom/sun/midp/midlet/CurrentMIDletSuite;"),
        }, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic | kSynchronized, "getScheduler",
                   "()Lcom/sun/midp/midlet/Scheduler;"),
            method(kPublic, "getMIDletSuite",
                   "()Lcom/sun/midp/midlet/MIDletSuite;"),
            method(kPublic, "getActiveMIDlet",
                   "()Ljavax/microedition/midlet/MIDlet;"),
        });
    }
    if (name == "com/siemens/mp/game/Light") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic | kStatic, "setLightOn", "()V"),
            method(kPublic | kStatic, "setLightOff", "()V"),
        });
    }
    if (name == "com/siemens/mp/game/Vibrator") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic | kStatic, "startVibrator", "()V"),
            method(kPublic | kStatic, "stopVibrator", "()V"),
            method(kPublic | kStatic, "triggerVibrator", "(I)V"),
        });
    }
    if (name == "com/samsung/util/LCDLight") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic | kStatic, "on", "(I)V"),
            method(kPublic | kStatic, "off", "()V"),
        });
    }
    if (name == "com/motorola/multimedia/Lighting") {
        return make_class(std::string(name), "java/lang/Object", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic | kStatic, "backlightOn", "()V"),
            method(kPublic | kStatic, "backlightOff", "()V"),
        });
    }
    return nullptr;
}

} // namespace

void register_vendor_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_vendor_class);
}

} // namespace phoneme::vm
