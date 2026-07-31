/*
 * Remote keyboard and pointer input for the headless LibVNCServer port.
 */

#include <rfb/keysym.h>

#include <kni.h>
#include <fbapp_export.h>
#include <keymap_input.h>
#include <midp_input_port.h>

#include "rfb_port.h"

static int map_rfb_keysym(unsigned int keysym) {
    switch (keysym) {
    case XK_Up:
    case XK_KP_Up:
        return KEYMAP_KEY_UP;
    case XK_Down:
    case XK_KP_Down:
        return KEYMAP_KEY_DOWN;
    case XK_Left:
    case XK_KP_Left:
        return KEYMAP_KEY_LEFT;
    case XK_Right:
    case XK_KP_Right:
        return KEYMAP_KEY_RIGHT;

    case XK_Return:
    case XK_KP_Enter:
    case XK_space:
        return KEYMAP_KEY_SELECT;

    case XK_F1:
    case XK_Page_Up:
        return KEYMAP_KEY_SOFT1;
    case XK_F2:
    case XK_Page_Down:
        return KEYMAP_KEY_SOFT2;

    case XK_BackSpace:
    case XK_Delete:
        return KEYMAP_KEY_CLEAR;
    case XK_Escape:
    case XK_End:
        return KEYMAP_KEY_END;
    case XK_Home:
        return KEYMAP_MD_KEY_HOME;
    case XK_F12:
        return KEYMAP_KEY_SCREEN_ROT;

    case XK_0:
    case XK_KP_0:
        return KEYMAP_KEY_0;
    case XK_1:
    case XK_KP_1:
        return KEYMAP_KEY_1;
    case XK_2:
    case XK_KP_2:
        return KEYMAP_KEY_2;
    case XK_3:
    case XK_KP_3:
        return KEYMAP_KEY_3;
    case XK_4:
    case XK_KP_4:
        return KEYMAP_KEY_4;
    case XK_5:
    case XK_KP_5:
        return KEYMAP_KEY_5;
    case XK_6:
    case XK_KP_6:
        return KEYMAP_KEY_6;
    case XK_7:
    case XK_KP_7:
        return KEYMAP_KEY_7;
    case XK_8:
    case XK_KP_8:
        return KEYMAP_KEY_8;
    case XK_9:
    case XK_KP_9:
        return KEYMAP_KEY_9;

    case XK_asterisk:
    case XK_KP_Multiply:
        return KEYMAP_KEY_ASTERISK;
    case XK_numbersign:
        return KEYMAP_KEY_POUND;
    default:
        break;
    }

    /* Preserve printable ASCII for MIDP text fields. */
    if (keysym >= 0x20 && keysym <= 0x7e) {
        return (int)keysym;
    }

    return KEYMAP_KEY_INVALID;
}

void handle_key_port(MidpReentryData* pNewSignal,
        MidpEvent* pNewMidpEvent) {
    PhoneMeRfbKeyEvent event;
    int midpKeyCode;

    if (!phoneme_rfb_read_key_event(&event)) {
        return;
    }

    midpKeyCode = map_rfb_keysym(event.keysym);
    fbapp_map_keycode_to_event(
        pNewSignal,
        pNewMidpEvent,
        midpKeyCode,
        event.pressed ? KNI_TRUE : KNI_FALSE,
        KNI_FALSE);
}

static int clamp_coordinate(int value, int maximum) {
    if (value < 0) {
        return 0;
    }
    if (value >= maximum) {
        return maximum - 1;
    }
    return value;
}

void handle_pointer_port(MidpReentryData* pNewSignal,
        MidpEvent* pNewMidpEvent) {
    PhoneMeRfbPointerEvent event;
    static int previousX = 0;
    static int previousY = 0;
    static int previousDown = 0;
    int hardwareId;
    int width;
    int height;
    int x;
    int y;
    int down;
    int action = -1;

    if (!phoneme_rfb_read_pointer_event(&event)) {
        return;
    }

    hardwareId = fbapp_get_current_hardwareId();
    width = fbapp_get_screen_width(hardwareId);
    height = fbapp_get_screen_height(hardwareId);

    if (fbapp_get_reverse_orientation(hardwareId)) {
        int outputWidth = height;
        int outputHeight = width;
        int offsetX = (320 - outputWidth) / 2;
        int offsetY = (240 - outputHeight) / 2;
        x = width - 1 - (event.y - offsetY);
        y = event.x - offsetX;
    } else {
        x = event.x - fbapp_get_screen_x(hardwareId);
        y = event.y - fbapp_get_screen_y(hardwareId);
    }

    x = clamp_coordinate(x, width);
    y = clamp_coordinate(y, height);
    down = (event.buttonMask & 1) != 0;

    if (!previousDown && down) {
        action = KEYMAP_STATE_PRESSED;
    } else if (previousDown && !down) {
        action = KEYMAP_STATE_RELEASED;
    } else if (down && (x != previousX || y != previousY)) {
        action = KEYMAP_STATE_DRAGGED;
    }

    previousX = x;
    previousY = y;
    previousDown = down;

    if (action == -1) {
        return;
    }

    pNewMidpEvent->type = MIDP_PEN_EVENT;
    pNewMidpEvent->X_POS = x;
    pNewMidpEvent->Y_POS = y;
    pNewMidpEvent->ACTION = action;
    pNewSignal->waitingFor = UI_SIGNAL;
}

jboolean has_pending_key_port(void) {
    return KNI_FALSE;
}

void handle_repeated_key_port(int midpKeyCode, jboolean isPressed) {
    (void)midpKeyCode;
    (void)isPressed;
}
