/*
 * Native SDL3 RGB565 framebuffer backend for phoneME.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include <fbport_export.h>
#include <gxj_putpixel.h>
#include <gxj_screen_buffer.h>
#include <midp_global_status.h>

#include "sdl3_port.h"

#define PHONEME_SDL3_WIDTH  320
#define PHONEME_SDL3_HEIGHT 240
#define PHONEME_SDL3_DEFAULT_SCALE 2
#define PHONEME_SDL3_QUEUE_SIZE 128

/** System offscreen buffer used by LCDUI. */
gxj_screen_buffer gxj_system_screen_buffer;

static SDL_Window* sdlWindow = NULL;
static SDL_Renderer* sdlRenderer = NULL;
static SDL_Texture* sdlTexture = NULL;
static gxj_pixel_type* sdlPixels = NULL;

static PhoneMeSdl3KeyEvent keyQueue[PHONEME_SDL3_QUEUE_SIZE];
static unsigned int keyReadIndex = 0;
static unsigned int keyWriteIndex = 0;
static PhoneMeSdl3PointerEvent pointerQueue[PHONEME_SDL3_QUEUE_SIZE];
static unsigned int pointerReadIndex = 0;
static unsigned int pointerWriteIndex = 0;
static int pointerButtonMask = 0;
static unsigned int queue_next(unsigned int index) {
    return (index + 1U) % PHONEME_SDL3_QUEUE_SIZE;
}

static int parse_env_int(const char* name, int defaultValue,
        int minimum, int maximum) {
    const char* text = getenv(name);
    char* end = NULL;
    long value;

    if (text == NULL || *text == '\0') {
        return defaultValue;
    }

    errno = 0;
    value = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
            value < minimum || value > maximum) {
        fprintf(stderr, "Ignoring invalid %s=%s\n", name, text);
        return defaultValue;
    }

    return (int)value;
}

static void enqueue_key(unsigned int keycode, int pressed, int repeat) {
    unsigned int next = queue_next(keyWriteIndex);
    PhoneMeSdl3KeyEvent* event;

    if (next == keyReadIndex) {
        keyReadIndex = queue_next(keyReadIndex);
    }

    event = &keyQueue[keyWriteIndex];
    event->keycode = keycode;
    event->pressed = pressed;
    event->repeat = repeat;
    keyWriteIndex = next;
}

static void enqueue_pointer(int x, int y, int buttonMask) {
    unsigned int next = queue_next(pointerWriteIndex);
    PhoneMeSdl3PointerEvent* event;

    if (next == pointerReadIndex) {
        pointerReadIndex = queue_next(pointerReadIndex);
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= PHONEME_SDL3_WIDTH) x = PHONEME_SDL3_WIDTH - 1;
    if (y >= PHONEME_SDL3_HEIGHT) y = PHONEME_SDL3_HEIGHT - 1;

    event = &pointerQueue[pointerWriteIndex];
    event->x = x;
    event->y = y;
    event->buttonMask = buttonMask;
    pointerWriteIndex = next;
}

static void render_frame(void) {
    if (sdlRenderer == NULL || sdlTexture == NULL || sdlPixels == NULL) {
        return;
    }

    if (!SDL_UpdateTexture(sdlTexture, NULL, sdlPixels,
            PHONEME_SDL3_WIDTH * (int)sizeof(gxj_pixel_type))) {
        fprintf(stderr, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return;
    }

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    SDL_RenderTexture(sdlRenderer, sdlTexture, NULL, NULL);
    SDL_RenderPresent(sdlRenderer);
}

void phoneme_sdl3_pump_events(void) {
    SDL_Event event;

    if (sdlWindow == NULL) {
        return;
    }

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            enqueue_key((unsigned int)SDLK_ESCAPE, 1, 0);
            enqueue_key((unsigned int)SDLK_ESCAPE, 0, 0);
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            enqueue_key((unsigned int)event.key.key,
                event.type == SDL_EVENT_KEY_DOWN ? 1 : 0,
                event.key.repeat ? 1 : 0);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (!SDL_ConvertEventToRenderCoordinates(sdlRenderer, &event)) {
                break;
            }
            if (event.button.button == SDL_BUTTON_LEFT) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    pointerButtonMask |= 1;
                } else {
                    pointerButtonMask &= ~1;
                }
            }
            enqueue_pointer((int)event.button.x, (int)event.button.y,
                pointerButtonMask);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            if (!SDL_ConvertEventToRenderCoordinates(sdlRenderer, &event)) {
                break;
            }
            enqueue_pointer((int)event.motion.x, (int)event.motion.y,
                pointerButtonMask);
            break;

        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
            render_frame();
            break;

        default:
            break;
        }
    }
}

int phoneme_sdl3_has_pending_key(void) {
    return keyReadIndex != keyWriteIndex;
}

int phoneme_sdl3_has_pending_pointer(void) {
    return pointerReadIndex != pointerWriteIndex;
}

int phoneme_sdl3_read_key_event(PhoneMeSdl3KeyEvent* event) {
    if (event == NULL || keyReadIndex == keyWriteIndex) {
        return 0;
    }

    *event = keyQueue[keyReadIndex];
    keyReadIndex = queue_next(keyReadIndex);
    return 1;
}

int phoneme_sdl3_read_pointer_event(PhoneMeSdl3PointerEvent* event) {
    if (event == NULL || pointerReadIndex == pointerWriteIndex) {
        return 0;
    }

    *event = pointerQueue[pointerReadIndex];
    pointerReadIndex = queue_next(pointerReadIndex);
    return 1;
}

int getKeyboardFd(void) {
    return -1;
}

int getMouseFd(void) {
    return -1;
}

void initScreenBuffer(int width, int height) {
    if (gxj_init_screen_buffer(width, height) != ALL_OK) {
        fprintf(stderr, "Failed to allocate phoneME screen buffer\n");
        exit(1);
    }
}

void connectFrameBuffer(void) {
    int scale = parse_env_int("PHONEME_SCALE",
        PHONEME_SDL3_DEFAULT_SCALE, 1, 8);
    int windowWidth = PHONEME_SDL3_WIDTH * scale;
    int windowHeight = PHONEME_SDL3_HEIGHT * scale;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        exit(1);
    }

    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    sdlWindow = SDL_CreateWindow("phoneME - J2ME",
        windowWidth, windowHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (sdlWindow == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        exit(1);
    }

    sdlRenderer = SDL_CreateRenderer(sdlWindow, NULL);
    if (sdlRenderer == NULL) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        exit(1);
    }

    if (!SDL_SetRenderLogicalPresentation(sdlRenderer,
            PHONEME_SDL3_WIDTH, PHONEME_SDL3_HEIGHT,
            SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        fprintf(stderr, "SDL logical presentation failed: %s\n", SDL_GetError());
        exit(1);
    }

    sdlTexture = SDL_CreateTexture(sdlRenderer,
        SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
        PHONEME_SDL3_WIDTH, PHONEME_SDL3_HEIGHT);
    if (sdlTexture == NULL) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetTextureScaleMode(sdlTexture, SDL_SCALEMODE_NEAREST);

    sdlPixels = (gxj_pixel_type*)calloc(
        (size_t)PHONEME_SDL3_WIDTH * PHONEME_SDL3_HEIGHT,
        sizeof(gxj_pixel_type));
    if (sdlPixels == NULL) {
        fprintf(stderr, "Failed to allocate SDL framebuffer\n");
        exit(1);
    }

    render_frame();
    fprintf(stderr,
        "phoneME SDL3 ready: native macOS window, 320x240 RGB565, scale %dx\n",
        scale);
}

void reverseScreenOrientation(void) {
    gxj_rotate_screen_buffer(KNI_FALSE);
    clearScreen();
}

void resizeScreenBuffer(int width, int height) {
    if (gxj_resize_screen_buffer(width, height) != ALL_OK) {
        fprintf(stderr, "Failed to resize phoneME screen buffer\n");
        exit(1);
    }
}

void clearScreen(void) {
    if (sdlPixels != NULL) {
        memset(sdlPixels, 0,
            (size_t)PHONEME_SDL3_WIDTH * PHONEME_SDL3_HEIGHT *
            sizeof(gxj_pixel_type));
        render_frame();
    }
}

int getScreenX(int screenRotated) {
    int lcdWidth = screenRotated ? PHONEME_SDL3_HEIGHT : PHONEME_SDL3_WIDTH;
    int width = gxj_system_screen_buffer.width;
    return lcdWidth > width ? (lcdWidth - width) / 2 : 0;
}

int getScreenY(int screenRotated) {
    int lcdHeight = screenRotated ? PHONEME_SDL3_WIDTH : PHONEME_SDL3_HEIGHT;
    int height = gxj_system_screen_buffer.height;
    return lcdHeight > height ? (lcdHeight - height) / 2 : 0;
}

static int clip_rectangle(int* x1, int* y1, int* x2, int* y2,
        int width, int height) {
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x2 > width) *x2 = width;
    if (*y2 > height) *y2 = height;
    return *x1 < *x2 && *y1 < *y2;
}

void refreshScreenNormal(int x1, int y1, int x2, int y2) {
    const gxj_pixel_type* src = gxj_system_screen_buffer.pixelData;
    int width = gxj_system_screen_buffer.width;
    int height = gxj_system_screen_buffer.height;
    int dstX;
    int dstY;
    int y;

    if (sdlPixels == NULL || src == NULL) {
        return;
    }
    if (width > PHONEME_SDL3_WIDTH || height > PHONEME_SDL3_HEIGHT) {
        fprintf(stderr, "phoneME screen %dx%d exceeds SDL surface 320x240\n",
            width, height);
        return;
    }
    if (!clip_rectangle(&x1, &y1, &x2, &y2, width, height)) {
        return;
    }

    dstX = (PHONEME_SDL3_WIDTH - width) / 2;
    dstY = (PHONEME_SDL3_HEIGHT - height) / 2;

    for (y = y1; y < y2; ++y) {
        memcpy(sdlPixels + (dstY + y) * PHONEME_SDL3_WIDTH + dstX + x1,
            src + y * width + x1,
            (size_t)(x2 - x1) * sizeof(gxj_pixel_type));
    }

    render_frame();
    phoneme_sdl3_pump_events();
}

void refreshScreenRotated(int x1, int y1, int x2, int y2) {
    const gxj_pixel_type* src = gxj_system_screen_buffer.pixelData;
    int width = gxj_system_screen_buffer.width;
    int height = gxj_system_screen_buffer.height;
    int outputWidth = height;
    int outputHeight = width;
    int dstX;
    int dstY;
    int x;
    int y;

    if (sdlPixels == NULL || src == NULL) {
        return;
    }
    if (outputWidth > PHONEME_SDL3_WIDTH ||
            outputHeight > PHONEME_SDL3_HEIGHT) {
        fprintf(stderr,
            "Rotated phoneME screen %dx%d exceeds SDL surface 320x240\n",
            outputWidth, outputHeight);
        return;
    }
    if (!clip_rectangle(&x1, &y1, &x2, &y2, width, height)) {
        return;
    }

    dstX = (PHONEME_SDL3_WIDTH - outputWidth) / 2;
    dstY = (PHONEME_SDL3_HEIGHT - outputHeight) / 2;

    for (y = y1; y < y2; ++y) {
        for (x = x1; x < x2; ++x) {
            int rotatedX = y;
            int rotatedY = width - x - 1;
            sdlPixels[(dstY + rotatedY) * PHONEME_SDL3_WIDTH +
                dstX + rotatedX] = src[y * width + x];
        }
    }

    render_frame();
    phoneme_sdl3_pump_events();
}

void finalizeFrameBuffer(void) {
    if (sdlTexture != NULL) {
        SDL_DestroyTexture(sdlTexture);
        sdlTexture = NULL;
    }
    if (sdlRenderer != NULL) {
        SDL_DestroyRenderer(sdlRenderer);
        sdlRenderer = NULL;
    }
    if (sdlWindow != NULL) {
        SDL_DestroyWindow(sdlWindow);
        sdlWindow = NULL;
    }

    free(sdlPixels);
    sdlPixels = NULL;
    gxj_free_screen_buffer();
    SDL_Quit();
}
