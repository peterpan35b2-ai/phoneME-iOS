/*
 * SDL3 keyboard and pointer input for the native phoneME window.
 */

#include <SDL3/SDL.h>

#include <kni.h>
#include <fbapp_export.h>
#include <keymap_input.h>
#include <midp_input_port.h>

#include "sdl3_port.h"

static int map_sdl_keycode(unsigned int keycode) {
    switch ((SDL_Keycode)keycode) {
    case SDLK_UP:
    case SDLK_KP_8:
        return KEYMAP_KEY_UP;
    case SDLK_DOWN:
    case SDLK_KP_2:
        return KEYMAP_KEY_DOWN;
    case SDLK_LEFT:
    case SDLK_KP_4:
        return KEYMAP_KEY_LEFT;
    case SDLK_RIGHT:
    case SDLK_KP_6:
        return KEYMAP_KEY_RIGHT;

    case SDLK_RETURN:
    case SDLK_RETURN2:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
    case SDLK_KP_5:
        return KEYMAP_KEY_SELECT;

    case SDLK_F1:
    case SDLK_PAGEUP:
    case SDLK_LEFTBRACKET:
        return KEYMAP_KEY_SOFT1;
    case SDLK_F2:
    case SDLK_PAGEDOWN:
    case SDLK_RIGHTBRACKET:
        return KEYMAP_KEY_SOFT2;

    case SDLK_BACKSPACE:
    case SDLK_DELETE:
        return KEYMAP_KEY_CLEAR;
    case SDLK_ESCAPE:
    case SDLK_END:
        return KEYMAP_KEY_END;
    case SDLK_HOME:
        return KEYMAP_MD_KEY_HOME;
    case SDLK_F12:
        return KEYMAP_KEY_SCREEN_ROT;

    case SDLK_0:
        return KEYMAP_KEY_0;
    case SDLK_1:
        return KEYMAP_KEY_1;
    case SDLK_2:
        return KEYMAP_KEY_2;
    case SDLK_3:
        return KEYMAP_KEY_3;
    case SDLK_4:
        return KEYMAP_KEY_4;
    case SDLK_5:
        return KEYMAP_KEY_5;
    case SDLK_6:
        return KEYMAP_KEY_6;
    case SDLK_7:
        return KEYMAP_KEY_7;
    case SDLK_8:
        return KEYMAP_KEY_8;
    case SDLK_9:
        return KEYMAP_KEY_9;

    case SDLK_ASTERISK:
    case SDLK_KP_MULTIPLY:
        return KEYMAP_KEY_ASTERISK;
    case SDLK_HASH:
        return KEYMAP_KEY_POUND;
    default:
        break;
    }

    /* Preserve printable ASCII for MIDP text fields. */
    if (keycode >= 0x20U && keycode <= 0x7eU) {
        return (int)keycode;
    }

    return KEYMAP_KEY_INVALID;
}

void handle_key_port(MidpReentryData* pNewSignal,
        MidpEvent* pNewMidpEvent) {
    PhoneMeSdl3KeyEvent event;
    int midpKeyCode;

    if (!phoneme_sdl3_read_key_event(&event)) {
        return;
    }

    midpKeyCode = map_sdl_keycode(event.keycode);
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
    PhoneMeSdl3PointerEvent event;
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

    if (!phoneme_sdl3_read_pointer_event(&event)) {
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
    phoneme_sdl3_pump_events();
    return phoneme_sdl3_has_pending_key() ? KNI_TRUE : KNI_FALSE;
}

void handle_repeated_key_port(int midpKeyCode, jboolean isPressed) {
    (void)midpKeyCode;
    (void)isPressed;
}
