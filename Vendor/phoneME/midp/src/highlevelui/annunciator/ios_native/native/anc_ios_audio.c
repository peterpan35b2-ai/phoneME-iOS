#include <anc_audio.h>

extern int phoneme_ios_media_play_tone(int note, int duration_ms, int volume);

jboolean anc_play_sound(AncSoundType soundType) {
    int note = 84;
    int duration = 120;

    switch (soundType) {
        case ANC_SOUND_WARNING:
            note = 76;
            duration = 160;
            break;
        case ANC_SOUND_ERROR:
            note = 52;
            duration = 240;
            break;
        case ANC_SOUND_ALARM:
            note = 88;
            duration = 320;
            break;
        default:
            break;
    }

    return phoneme_ios_media_play_tone(note, duration, 80)
        ? KNI_TRUE
        : KNI_FALSE;
}
