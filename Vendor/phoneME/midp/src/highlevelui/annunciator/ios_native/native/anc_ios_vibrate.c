#include <anc_vibrate.h>

extern int phoneme_ios_device_set_vibrate(int enabled);

jboolean anc_start_vibrate(void) {
    return phoneme_ios_device_set_vibrate(1) ? KNI_TRUE : KNI_FALSE;
}

jboolean anc_stop_vibrate(void) {
    return phoneme_ios_device_set_vibrate(0) ? KNI_TRUE : KNI_FALSE;
}
