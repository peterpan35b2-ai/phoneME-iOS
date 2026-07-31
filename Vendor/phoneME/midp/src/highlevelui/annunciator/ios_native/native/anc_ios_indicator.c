#include <anc_indicators.h>

extern int phoneme_ios_device_set_backlight(int mode);

void anc_show_trusted_indicator(jboolean isTrusted) {
    (void)isTrusted;
}

void anc_set_network_indicator(int counter) {
    (void)counter;
}

jboolean anc_show_backlight(AncBacklightState mode) {
    return phoneme_ios_device_set_backlight((int)mode)
        ? KNI_TRUE
        : KNI_FALSE;
}

void anc_toggle_home_icon(jboolean isHomeOn) {
    (void)isHomeOn;
}
