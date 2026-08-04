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
    if (name == "com/sun/midp/midlet/Scheduler") {
        return make_class(std::string(name), "java/lang/Object",
                          kOrdinary | kFinal, {}, {
            method(kPrivate, "<init>", "()V"),
            method(kPublic | kStatic, "getScheduler", "()Lcom/sun/midp/midlet/Scheduler;"),
            method(kPublic, "getActiveMIDlet", "()Ljavax/microedition/midlet/MIDlet;"),
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
