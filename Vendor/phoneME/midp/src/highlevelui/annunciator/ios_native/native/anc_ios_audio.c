#include <anc_audio.h>

jboolean anc_play_sound(AncSoundType soundType) {
    (void)soundType;

    /* Native iOS LCDUI alerts are intentionally silent. */
    return KNI_FALSE;
}
