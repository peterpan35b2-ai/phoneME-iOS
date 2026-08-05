#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "phoneme/classfile/ClassFile.hpp"
#include "phoneme/vm/BuiltinClassRegistry.hpp"

namespace {

constexpr phoneme::u16 kPublic = 0x0001U;
constexpr phoneme::u16 kStatic = 0x0008U;

void require(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "VendorCompatibilityTests: " << message << '\n';
    std::exit(1);
}

void require_method(const phoneme::classfile::ClassFile& type,
                    std::string_view name,
                    std::string_view descriptor,
                    bool is_static,
                    bool is_public = true) {
    const auto* method = type.find_method(name, descriptor);
    require(method != nullptr,
            std::string("expected vendor method is missing: ") +
                type.name() + "." + std::string(name) +
                std::string(descriptor));
    require(((method->access_flags & kPublic) != 0U) == is_public,
            "vendor method has the wrong visibility");
    require(((method->access_flags & kStatic) != 0U) == is_static,
            "vendor method has the wrong static shape");
}

phoneme::vm::BuiltinClassRegistry::ClassPtr require_class(
    const phoneme::vm::BuiltinClassRegistry& registry,
    std::string_view name) {
    const auto type = registry.find(name);
    require(type != nullptr, "expected vendor class is missing");
    require(type->super_name() == "java/lang/Object",
            "vendor class must extend java.lang.Object");
    return type;
}

} // namespace

int main() {
    phoneme::vm::BuiltinClassRegistry registry;
    phoneme::vm::register_vendor_classes(registry);

    const auto samsung_audio = require_class(
        registry, "com/samsung/util/AudioClip");
    require_method(*samsung_audio, "<init>", "(ILjava/lang/String;)V", false);
    require_method(*samsung_audio, "<init>", "(I[BII)V", false);
    require_method(*samsung_audio, "play", "(II)V", false);
    require_method(*samsung_audio, "stop", "()V", false);
    require_method(*samsung_audio, "pause", "()V", false);
    require_method(*samsung_audio, "resume", "()V", false);

    const auto sprint_clip = require_class(
        registry, "com/sprintpcs/media/Clip");
    require_method(*sprint_clip, "<init>",
                   "(Ljava/lang/String;Ljava/lang/String;II)V", false);
    require_method(*sprint_clip, "<init>",
                   "([BLjava/lang/String;II)V", false);
    require_method(*sprint_clip, "getPriority", "()I", false);
    require_method(*sprint_clip, "getVibration", "()I", false);
    require_method(*sprint_clip, "getPlayer",
                   "()Ljavax/microedition/media/Player;", false);

    const auto sprint_player = require_class(
        registry, "com/sprintpcs/media/Player");
    require_method(*sprint_player, "<init>", "()V", false, false);
    require_method(*sprint_player, "play",
                   "(Lcom/sprintpcs/media/Clip;I)V", true);
    require_method(*sprint_player, "playBackground",
                   "(Lcom/sprintpcs/media/Clip;I)V", true);
    require_method(*sprint_player, "stop", "()V", true);

    const auto siemens_sms = require_class(
        registry, "com/siemens/mp/gsm/SMS");
    require_method(*siemens_sms, "<init>", "()V", false, false);
    require_method(*siemens_sms, "send",
                   "(Ljava/lang/String;Ljava/lang/String;)I", true);

    const auto vodafone_encoder = require_class(
        registry, "com/vodafone/util/ImageEncoder");
    require_method(*vodafone_encoder, "<init>", "()V", false, false);
    require_method(*vodafone_encoder, "createEncoder",
                   "(I)Lcom/vodafone/util/ImageEncoder;", true);
    require_method(*vodafone_encoder, "encodeOffscreen",
                   "(Ljavax/microedition/lcdui/Image;IIII)[B", false);

    const auto scheduler = require_class(
        registry, "com/sun/midp/midlet/Scheduler");
    require_method(*scheduler, "<init>", "()V", false, false);
    require_method(*scheduler, "getScheduler",
                   "()Lcom/sun/midp/midlet/Scheduler;", true);
    require_method(*scheduler, "getMIDletSuite",
                   "()Lcom/sun/midp/midlet/MIDletSuite;", false);
    require_method(*scheduler, "getActiveMIDlet",
                   "()Ljavax/microedition/midlet/MIDlet;", false);

    const auto midlet_suite = registry.find(
        "com/sun/midp/midlet/MIDletSuite");
    require(midlet_suite != nullptr,
            "phoneME MIDletSuite compatibility interface is missing");
    require_method(*midlet_suite, "getProperty",
                   "(Ljava/lang/String;)Ljava/lang/String;", false);
    require_method(*midlet_suite, "checkPermission",
                   "(Ljava/lang/String;)I", false);
    require_method(*midlet_suite, "getID", "()I", false);
    require_method(*midlet_suite, "isTrusted", "()Z", false);

    const auto siemens_light = require_class(
        registry, "com/siemens/mp/game/Light");
    require_method(*siemens_light, "<init>", "()V", false);
    require_method(*siemens_light, "setLightOn", "()V", true);
    require_method(*siemens_light, "setLightOff", "()V", true);

    const auto siemens_vibrator = require_class(
        registry, "com/siemens/mp/game/Vibrator");
    require_method(*siemens_vibrator, "<init>", "()V", false);
    require_method(*siemens_vibrator, "startVibrator", "()V", true);
    require_method(*siemens_vibrator, "stopVibrator", "()V", true);
    require_method(*siemens_vibrator, "triggerVibrator", "(I)V", true);

    const auto samsung_light = require_class(
        registry, "com/samsung/util/LCDLight");
    require_method(*samsung_light, "<init>", "()V", false);
    require_method(*samsung_light, "on", "(I)V", true);
    require_method(*samsung_light, "off", "()V", true);

    const auto motorola_lighting = require_class(
        registry, "com/motorola/multimedia/Lighting");
    require_method(*motorola_lighting, "<init>", "()V", false);
    require_method(*motorola_lighting, "backlightOn", "()V", true);
    require_method(*motorola_lighting, "backlightOff", "()V", true);

    require(registry.find("com/unknown/vendor/Extension") == nullptr,
            "vendor registry must not synthesize unknown classes");

    std::cout << "Vendor compatibility registry tests passed\n";
    return 0;
}
