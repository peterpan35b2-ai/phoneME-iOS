#include "phoneme/vm/BuiltinClassRegistry.hpp"

#include "BuiltinClassSupport.hpp"

namespace phoneme::vm {
namespace {

using namespace builtin;

[[nodiscard]] ClassPtr build_media_class(std::string_view name) {
    if (name == "com/nokia/mid/sound/SoundListener") {
        return make_class(
            "com/nokia/mid/sound/SoundListener",
            "java/lang/Object",
            kPublic | kInterface | kAbstract,
            {},
            {method(kPublic | kAbstract,
                    "soundStateChanged",
                    "(Lcom/nokia/mid/sound/Sound;I)V")});
    }
    if (name == "com/nokia/mid/sound/Sound") {
        return make_class(
            "com/nokia/mid/sound/Sound",
            "java/lang/Object",
            kOrdinary,
            {
                field(kPrivate, "nativeId", "I"),
                field(kPrivate, "state", "I"),
                field(kPrivate, "gain", "I"),
                field(kPrivate, "listener",
                      "Lcom/nokia/mid/sound/SoundListener;"),
                field(kPrivate, "format", "I"),
                field(kPrivate, "toneFrequency", "I"),
                field(kPrivate, "toneDuration", "J"),
            },
            {
                method(kPublic, "<init>", "([BI)V"),
                method(kPublic, "<init>", "(IJ)V"),
                method(kPublic | kStatic, "getSupportedFormats", "()[I"),
                method(kPublic | kStatic, "getConcurrentSoundCount", "(I)I"),
                method(kPublic | kSynchronized, "init", "([BI)V"),
                method(kPublic | kSynchronized, "init", "(IJ)V"),
                method(kPublic, "play", "(I)V"),
                method(kPublic | kSynchronized, "stop", "()V"),
                method(kPublic | kSynchronized, "resume", "()V"),
                method(kPublic | kSynchronized, "release", "()V"),
                method(kPublic | kSynchronized, "getState", "()I"),
                method(kPublic | kSynchronized, "setGain", "(I)V"),
                method(kPublic | kSynchronized, "getGain", "()I"),
                method(kPublic | kSynchronized, "setSoundListener",
                       "(Lcom/nokia/mid/sound/SoundListener;)V"),
            });
    }
    if (name == "javax/microedition/media/MediaException") {
        return make_class("javax/microedition/media/MediaException",
                          "java/lang/Exception", kOrdinary, {}, {
            method(kPublic, "<init>", "()V"),
            method(kPublic, "<init>", "(Ljava/lang/String;)V"),
        });
    }
    if (name == "javax/microedition/media/Control") {
        return make_class("javax/microedition/media/Control",
                          "java/lang/Object",
                          kPublic | kInterface | kAbstract);
    }
    if (name == "javax/microedition/media/Controllable") {
        return make_class(
            "javax/microedition/media/Controllable",
            "java/lang/Object",
            kPublic | kInterface | kAbstract,
            {},
            {
                method(kPublic | kAbstract,
                       "getControl",
                       "(Ljava/lang/String;)Ljavax/microedition/media/Control;"),
                method(kPublic | kAbstract,
                       "getControls",
                       "()[Ljavax/microedition/media/Control;"),
            });
    }
    if (name == "javax/microedition/media/TimeBase") {
        return make_class("javax/microedition/media/TimeBase",
                          "java/lang/Object",
                          kPublic | kInterface | kAbstract,
                          {},
                          {method(kPublic | kAbstract, "getTime", "()J")});
    }
    if (name == "javax/microedition/media/PlayerListener") {
        return make_class(
            "javax/microedition/media/PlayerListener",
            "java/lang/Object",
            kPublic | kInterface | kAbstract,
            {
                field(kPublic | kStatic | kFinal, "STARTED", "Ljava/lang/String;"),
                field(kPublic | kStatic | kFinal, "STOPPED", "Ljava/lang/String;"),
                field(kPublic | kStatic | kFinal, "END_OF_MEDIA", "Ljava/lang/String;"),
                field(kPublic | kStatic | kFinal, "DURATION_UPDATED", "Ljava/lang/String;"),
                field(kPublic | kStatic | kFinal, "DEVICE_UNAVAILABLE", "Ljava/lang/String;"),
                field(kPublic | kStatic | kFinal, "DEVICE_AVAILABLE", "Ljava/lang/String;"),
                field(kPublic | kStatic | kFinal, "VOLUME_CHANGED", "Ljava/lang/String;"),
                field(kPublic | kStatic | kFinal, "ERROR", "Ljava/lang/String;"),
                field(kPublic | kStatic | kFinal, "CLOSED", "Ljava/lang/String;"),
            },
            {method(kPublic | kAbstract,
                    "playerUpdate",
                    "(Ljavax/microedition/media/Player;Ljava/lang/String;Ljava/lang/Object;)V")});
    }
    if (name == "javax/microedition/media/Player") {
        return make_class(
            "javax/microedition/media/Player",
            "java/lang/Object",
            kPublic | kInterface | kAbstract,
            {
                field(kPublic | kStatic | kFinal, "UNREALIZED", "I"),
                field(kPublic | kStatic | kFinal, "REALIZED", "I"),
                field(kPublic | kStatic | kFinal, "PREFETCHED", "I"),
                field(kPublic | kStatic | kFinal, "STARTED", "I"),
                field(kPublic | kStatic | kFinal, "CLOSED", "I"),
                field(kPublic | kStatic | kFinal, "TIME_UNKNOWN", "J"),
            },
            {
                method(kPublic | kAbstract, "realize", "()V"),
                method(kPublic | kAbstract, "prefetch", "()V"),
                method(kPublic | kAbstract, "start", "()V"),
                method(kPublic | kAbstract, "stop", "()V"),
                method(kPublic | kAbstract, "deallocate", "()V"),
                method(kPublic | kAbstract, "close", "()V"),
                method(kPublic | kAbstract,
                       "setTimeBase",
                       "(Ljavax/microedition/media/TimeBase;)V"),
                method(kPublic | kAbstract,
                       "getTimeBase",
                       "()Ljavax/microedition/media/TimeBase;"),
                method(kPublic | kAbstract, "setMediaTime", "(J)J"),
                method(kPublic | kAbstract, "getMediaTime", "()J"),
                method(kPublic | kAbstract, "getState", "()I"),
                method(kPublic | kAbstract, "getDuration", "()J"),
                method(kPublic | kAbstract,
                       "getContentType",
                       "()Ljava/lang/String;"),
                method(kPublic | kAbstract, "setLoopCount", "(I)V"),
                method(kPublic | kAbstract,
                       "addPlayerListener",
                       "(Ljavax/microedition/media/PlayerListener;)V"),
                method(kPublic | kAbstract,
                       "removePlayerListener",
                       "(Ljavax/microedition/media/PlayerListener;)V"),
                method(kPublic | kAbstract,
                       "getControl",
                       "(Ljava/lang/String;)Ljavax/microedition/media/Control;"),
                method(kPublic | kAbstract,
                       "getControls",
                       "()[Ljavax/microedition/media/Control;"),
            },
            {"javax/microedition/media/Controllable"});
    }
    if (name == "javax/microedition/media/control/VolumeControl") {
        return make_class(
            "javax/microedition/media/control/VolumeControl",
            "java/lang/Object",
            kPublic | kInterface | kAbstract,
            {},
            {
                method(kPublic | kAbstract, "setLevel", "(I)I"),
                method(kPublic | kAbstract, "getLevel", "()I"),
                method(kPublic | kAbstract, "setMute", "(Z)V"),
                method(kPublic | kAbstract, "isMuted", "()Z"),
            },
            {"javax/microedition/media/Control"});
    }
    if (name == "javax/microedition/media/control/ToneControl") {
        return make_class(
            "javax/microedition/media/control/ToneControl",
            "java/lang/Object",
            kPublic | kInterface | kAbstract,
            {
                field(kPublic | kStatic | kFinal, "VERSION", "B"),
                field(kPublic | kStatic | kFinal, "TEMPO", "B"),
                field(kPublic | kStatic | kFinal, "RESOLUTION", "B"),
                field(kPublic | kStatic | kFinal, "BLOCK_START", "B"),
                field(kPublic | kStatic | kFinal, "BLOCK_END", "B"),
                field(kPublic | kStatic | kFinal, "PLAY_BLOCK", "B"),
                field(kPublic | kStatic | kFinal, "SET_VOLUME", "B"),
                field(kPublic | kStatic | kFinal, "REPEAT", "B"),
                field(kPublic | kStatic | kFinal, "SILENCE", "B"),
                field(kPublic | kStatic | kFinal, "C4", "B"),
            },
            {method(kPublic | kAbstract, "setSequence", "([B)V")},
            {"javax/microedition/media/Control"});
    }
    if (name == "javax/microedition/media/SystemTimeBase") {
        return make_class(
            "javax/microedition/media/SystemTimeBase",
            "java/lang/Object",
            kOrdinary | kFinal,
            {},
            {
                method(kPublic, "<init>", "()V"),
                method(kPublic, "getTime", "()J"),
            },
            {"javax/microedition/media/TimeBase"});
    }
    if (name == "javax/microedition/media/IOSVolumeControl") {
        return make_class(
            "javax/microedition/media/IOSVolumeControl",
            "java/lang/Object",
            kOrdinary | kFinal,
            {field(kPrivate | kFinal,
                   "player",
                   "Ljavax/microedition/media/IOSPlayer;")},
            {
                method(kPrivate, "<init>", "()V"),
                method(kPublic, "setLevel", "(I)I"),
                method(kPublic, "getLevel", "()I"),
                method(kPublic, "setMute", "(Z)V"),
                method(kPublic, "isMuted", "()Z"),
            },
            {"javax/microedition/media/control/VolumeControl"});
    }
    if (name == "javax/microedition/media/IOSToneControl") {
        return make_class(
            "javax/microedition/media/IOSToneControl",
            "java/lang/Object",
            kOrdinary | kFinal,
            {field(kPrivate | kFinal,
                   "player",
                   "Ljavax/microedition/media/IOSPlayer;")},
            {
                method(kPrivate, "<init>", "()V"),
                method(kPublic, "setSequence", "([B)V"),
            },
            {"javax/microedition/media/control/ToneControl"});
    }
    if (name == "javax/microedition/media/IOSPlayer") {
        return make_class(
            "javax/microedition/media/IOSPlayer",
            "java/lang/Object",
            kOrdinary | kFinal,
            {
                field(kPrivate | kFinal, "nativeId", "I"),
                field(kPrivate,
                      "listeners",
                      "[Ljavax/microedition/media/PlayerListener;"),
                field(kPrivate, "listenerCount", "I"),
                field(kPrivate | kFinal,
                      "volumeControl",
                      "Ljavax/microedition/media/IOSVolumeControl;"),
                field(kPrivate | kFinal,
                      "toneControl",
                      "Ljavax/microedition/media/IOSToneControl;"),
                field(kPrivate | kFinal,
                      "contentType",
                      "Ljava/lang/String;"),
                field(kPrivate,
                      "timeBase",
                      "Ljavax/microedition/media/TimeBase;"),
            },
            {
                method(kPrivate, "<init>", "()V"),
                method(kPublic, "realize", "()V"),
                method(kPublic, "prefetch", "()V"),
                method(kPublic, "start", "()V"),
                method(kPublic, "stop", "()V"),
                method(kPublic, "deallocate", "()V"),
                method(kPublic, "close", "()V"),
                method(kPublic,
                       "setTimeBase",
                       "(Ljavax/microedition/media/TimeBase;)V"),
                method(kPublic,
                       "getTimeBase",
                       "()Ljavax/microedition/media/TimeBase;"),
                method(kPublic, "setMediaTime", "(J)J"),
                method(kPublic, "getMediaTime", "()J"),
                method(kPublic, "getState", "()I"),
                method(kPublic, "getDuration", "()J"),
                method(kPublic,
                       "getContentType",
                       "()Ljava/lang/String;"),
                method(kPublic, "setLoopCount", "(I)V"),
                method(kPublic,
                       "addPlayerListener",
                       "(Ljavax/microedition/media/PlayerListener;)V"),
                method(kPublic,
                       "removePlayerListener",
                       "(Ljavax/microedition/media/PlayerListener;)V"),
                method(kPublic,
                       "getControl",
                       "(Ljava/lang/String;)Ljavax/microedition/media/Control;"),
                method(kPublic,
                       "getControls",
                       "()[Ljavax/microedition/media/Control;"),
            },
            {"javax/microedition/media/Player"});
    }
    if (name == "javax/microedition/media/Manager") {
        return make_class(
            "javax/microedition/media/Manager",
            "java/lang/Object",
            kOrdinary | kFinal,
            {
                field(kPublic | kStatic | kFinal, "TONE_DEVICE_LOCATOR",
                      "Ljava/lang/String;"),
            },
            {
                method(kPrivate, "<init>", "()V"),
                method(kPublic | kStatic,
                       "getSupportedContentTypes",
                       "(Ljava/lang/String;)[Ljava/lang/String;"),
                method(kPublic | kStatic,
                       "getSupportedProtocols",
                       "(Ljava/lang/String;)[Ljava/lang/String;"),
                method(kPublic | kStatic,
                       "createPlayer",
                       "(Ljava/lang/String;)Ljavax/microedition/media/Player;"),
                method(kPublic | kStatic,
                       "createPlayer",
                       "(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;"),
                method(kPublic | kStatic, "playTone", "(III)V"),
                method(kPublic | kStatic,
                       "getSystemTimeBase",
                       "()Ljavax/microedition/media/TimeBase;"),
            });
    }
    return nullptr;
}

} // namespace

void register_media_classes(BuiltinClassRegistry& registry) {
    registry.add_factory(build_media_class);
}

} // namespace phoneme::vm
