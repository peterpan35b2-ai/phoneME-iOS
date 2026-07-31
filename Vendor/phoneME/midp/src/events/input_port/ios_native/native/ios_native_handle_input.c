/* Native keypad and pointer input for the in-process iOS host. */

#include <stdint.h>

#include <kni.h>
#include <fbapp_export.h>
#include <jvm.h>
#include <keymap_input.h>
#include <midp_input_port.h>

#include "ios_native_port.h"

void handle_key_port(
        MidpReentryData* pNewSignal,
        MidpEvent* pNewMidpEvent) {
    int32_t keyCode;
    int32_t pressed;

    if (!phoneme_ios_port_read_key(&keyCode, &pressed)) {
        return;
    }

    fbapp_map_keycode_to_event(
        pNewSignal,
        pNewMidpEvent,
        (int)keyCode,
        pressed ? KNI_TRUE : KNI_FALSE,
        KNI_FALSE
    );
}

void handle_pointer_port(
        MidpReentryData* pNewSignal,
        MidpEvent* pNewMidpEvent) {
    int32_t hostX;
    int32_t hostY;
    int32_t action;
    int hardwareId;
    int width;
    int height;
    int x;
    int y;

    if (!phoneme_ios_port_read_pointer(&hostX, &hostY, &action)) {
        return;
    }

    hardwareId = fbapp_get_current_hardwareId();
    width = fbapp_get_screen_width(hardwareId);
    height = fbapp_get_screen_height(hardwareId);

    if (fbapp_get_reverse_orientation(hardwareId)) {
        x = width - 1 - hostY;
        y = hostX;
    } else {
        x = hostX;
        y = hostY;
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= width) x = width - 1;
    if (y >= height) y = height - 1;

    pNewMidpEvent->type = MIDP_PEN_EVENT;
    pNewMidpEvent->X_POS = x;
    pNewMidpEvent->Y_POS = y;
    pNewMidpEvent->ACTION = (int)action;
    pNewSignal->waitingFor = UI_SIGNAL;
}

jboolean has_pending_key_port(void) {
    /* JVM_Stop must execute on the VM thread because it unwinds to the
       primordial VM stack. Swift only sets this flag from the host thread. */
    if (phoneme_ios_port_should_stop()) {
        JVM_Stop(0);
        return KNI_FALSE;
    }

    return phoneme_ios_port_has_pending_key() ? KNI_TRUE : KNI_FALSE;
}

void handle_repeated_key_port(int midpKeyCode, jboolean isPressed) {
    (void)midpKeyCode;
    (void)isPressed;
}
