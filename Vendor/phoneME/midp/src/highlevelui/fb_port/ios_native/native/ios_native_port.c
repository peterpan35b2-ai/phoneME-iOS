/*
 * Native in-process framebuffer and event queues for the iOS phoneME host.
 * LCDUI renders into its RGB565 software buffer; the Swift host reads a stable
 * RGBA8888 snapshot through the small C ABI.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fbport_export.h>
#include <gxj_putpixel.h>
#include <gxj_screen_buffer.h>
#include <midp_global_status.h>

#include "ios_native_port.h"

#define PHONEME_IOS_QUEUE_SIZE 128

typedef struct {
    int32_t key_code;
    int32_t pressed;
} PhoneMEIOSKeyEvent;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t action;
} PhoneMEIOSPointerEvent;

/** System offscreen buffer used by LCDUI. */
gxj_screen_buffer gxj_system_screen_buffer;

static pthread_mutex_t portMutex = PTHREAD_MUTEX_INITIALIZER;
static gxj_pixel_type* framePixels;
static int frameWidth;
static int frameHeight;
static int configuredWidth = 240;
static int configuredHeight = 320;
static PhoneMEIOSKeyEvent keyQueue[PHONEME_IOS_QUEUE_SIZE];
static unsigned int keyReadIndex;
static unsigned int keyWriteIndex;
static PhoneMEIOSPointerEvent pointerQueue[PHONEME_IOS_QUEUE_SIZE];
static unsigned int pointerReadIndex;
static unsigned int pointerWriteIndex;
static int stopRequested;

static unsigned int queue_next(unsigned int index) {
    return (index + 1U) % PHONEME_IOS_QUEUE_SIZE;
}

static int clip_rectangle(
        int* x1, int* y1, int* x2, int* y2, int width, int height) {
    if (*x1 < 0) *x1 = 0;
    if (*y1 < 0) *y1 = 0;
    if (*x2 > width) *x2 = width;
    if (*y2 > height) *y2 = height;
    return *x1 < *x2 && *y1 < *y2;
}

static int ensure_frame_buffer_locked(int width, int height) {
    gxj_pixel_type* replacement;
    size_t pixelCount;

    if (width <= 0 || height <= 0) {
        return 0;
    }
    if (framePixels != NULL && frameWidth == width && frameHeight == height) {
        return 1;
    }

    pixelCount = (size_t)width * (size_t)height;
    if (pixelCount > ((size_t)-1) / sizeof(gxj_pixel_type)) {
        return 0;
    }

    replacement = (gxj_pixel_type*)calloc(pixelCount, sizeof(gxj_pixel_type));
    if (replacement == NULL) {
        return 0;
    }

    free(framePixels);
    framePixels = replacement;
    frameWidth = width;
    frameHeight = height;
    return 1;
}

static uint8_t expand5(unsigned int value) {
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(unsigned int value) {
    return (uint8_t)((value << 2) | (value >> 4));
}

void phoneme_ios_port_configure_display(int32_t width, int32_t height) {
    if (width < 1 || height < 1 || width > 2048 || height > 2048) {
        return;
    }

    pthread_mutex_lock(&portMutex);
    configuredWidth = (int)width;
    configuredHeight = (int)height;
    pthread_mutex_unlock(&portMutex);
}

int32_t phoneme_ios_port_configured_width(void) {
    int32_t result;
    pthread_mutex_lock(&portMutex);
    result = (int32_t)configuredWidth;
    pthread_mutex_unlock(&portMutex);
    return result;
}

int32_t phoneme_ios_port_configured_height(void) {
    int32_t result;
    pthread_mutex_lock(&portMutex);
    result = (int32_t)configuredHeight;
    pthread_mutex_unlock(&portMutex);
    return result;
}

void phoneme_ios_port_reset(void) {
    pthread_mutex_lock(&portMutex);
    if (framePixels != NULL) {
        memset(
            framePixels,
            0,
            (size_t)frameWidth * (size_t)frameHeight * sizeof(gxj_pixel_type)
        );
    }
    keyReadIndex = 0;
    keyWriteIndex = 0;
    pointerReadIndex = 0;
    pointerWriteIndex = 0;
    stopRequested = 0;
    pthread_mutex_unlock(&portMutex);
}

void phoneme_ios_port_request_stop(void) {
    pthread_mutex_lock(&portMutex);
    stopRequested = 1;
    pthread_mutex_unlock(&portMutex);
}

int phoneme_ios_port_should_stop(void) {
    int result;
    pthread_mutex_lock(&portMutex);
    result = stopRequested;
    pthread_mutex_unlock(&portMutex);
    return result;
}

void phoneme_ios_port_send_key(int32_t key_code, int32_t pressed) {
    unsigned int next;

    pthread_mutex_lock(&portMutex);
    next = queue_next(keyWriteIndex);
    if (next == keyReadIndex) {
        keyReadIndex = queue_next(keyReadIndex);
    }
    keyQueue[keyWriteIndex].key_code = key_code;
    keyQueue[keyWriteIndex].pressed = pressed != 0;
    keyWriteIndex = next;
    pthread_mutex_unlock(&portMutex);
}

int phoneme_ios_port_has_pending_key(void) {
    int result;
    pthread_mutex_lock(&portMutex);
    result = keyReadIndex != keyWriteIndex;
    pthread_mutex_unlock(&portMutex);
    return result;
}

int phoneme_ios_port_read_key(int32_t* key_code, int32_t* pressed) {
    int result = 0;

    if (key_code == NULL || pressed == NULL) {
        return 0;
    }

    pthread_mutex_lock(&portMutex);
    if (keyReadIndex != keyWriteIndex) {
        *key_code = keyQueue[keyReadIndex].key_code;
        *pressed = keyQueue[keyReadIndex].pressed;
        keyReadIndex = queue_next(keyReadIndex);
        result = 1;
    }
    pthread_mutex_unlock(&portMutex);
    return result;
}

void phoneme_ios_port_send_pointer(
        int32_t x, int32_t y, int32_t action) {
    unsigned int next;

    pthread_mutex_lock(&portMutex);
    if (frameWidth <= 0 || frameHeight <= 0) {
        pthread_mutex_unlock(&portMutex);
        return;
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= frameWidth) x = frameWidth - 1;
    if (y >= frameHeight) y = frameHeight - 1;

    next = queue_next(pointerWriteIndex);
    if (next == pointerReadIndex) {
        pointerReadIndex = queue_next(pointerReadIndex);
    }
    pointerQueue[pointerWriteIndex].x = x;
    pointerQueue[pointerWriteIndex].y = y;
    pointerQueue[pointerWriteIndex].action = action;
    pointerWriteIndex = next;
    pthread_mutex_unlock(&portMutex);
}

int phoneme_ios_port_has_pending_pointer(void) {
    int result;
    pthread_mutex_lock(&portMutex);
    result = pointerReadIndex != pointerWriteIndex;
    pthread_mutex_unlock(&portMutex);
    return result;
}

int phoneme_ios_port_read_pointer(
        int32_t* x, int32_t* y, int32_t* action) {
    int result = 0;

    if (x == NULL || y == NULL || action == NULL) {
        return 0;
    }

    pthread_mutex_lock(&portMutex);
    if (pointerReadIndex != pointerWriteIndex) {
        *x = pointerQueue[pointerReadIndex].x;
        *y = pointerQueue[pointerReadIndex].y;
        *action = pointerQueue[pointerReadIndex].action;
        pointerReadIndex = queue_next(pointerReadIndex);
        result = 1;
    }
    pthread_mutex_unlock(&portMutex);
    return result;
}

int32_t phoneme_ios_port_frame_width(void) {
    int result;
    pthread_mutex_lock(&portMutex);
    result = frameWidth;
    pthread_mutex_unlock(&portMutex);
    return result;
}

int32_t phoneme_ios_port_frame_height(void) {
    int result;
    pthread_mutex_lock(&portMutex);
    result = frameHeight;
    pthread_mutex_unlock(&portMutex);
    return result;
}

int32_t phoneme_ios_port_copy_frame_rgba(
        uint8_t* destination,
        int32_t capacity,
        int32_t* width,
        int32_t* height) {
    int32_t required;
    int index;

    pthread_mutex_lock(&portMutex);
    if (width != NULL) *width = frameWidth;
    if (height != NULL) *height = frameHeight;

    if (framePixels == NULL || frameWidth <= 0 || frameHeight <= 0 ||
            (size_t)frameWidth * (size_t)frameHeight > INT32_MAX / 4) {
        pthread_mutex_unlock(&portMutex);
        return 0;
    }

    required = frameWidth * frameHeight * 4;
    if (destination == NULL || capacity < required) {
        pthread_mutex_unlock(&portMutex);
        return required;
    }

    for (index = 0; index < frameWidth * frameHeight; ++index) {
        unsigned int pixel = (unsigned int)framePixels[index];
        destination[index * 4 + 0] = expand5((pixel >> 11) & 0x1fU);
        destination[index * 4 + 1] = expand6((pixel >> 5) & 0x3fU);
        destination[index * 4 + 2] = expand5(pixel & 0x1fU);
        destination[index * 4 + 3] = 0xffU;
    }
    pthread_mutex_unlock(&portMutex);
    return required;
}

int getKeyboardFd(void) {
    return -1;
}

int getMouseFd(void) {
    return -1;
}

void initScreenBuffer(int width, int height) {
    if (gxj_init_screen_buffer(width, height) != ALL_OK) {
        abort();
    }

    pthread_mutex_lock(&portMutex);
    if (!ensure_frame_buffer_locked(width, height)) {
        pthread_mutex_unlock(&portMutex);
        abort();
    }
    pthread_mutex_unlock(&portMutex);
}

void connectFrameBuffer(void) {
    phoneme_ios_port_reset();
}

void reverseScreenOrientation(void) {
    gxj_rotate_screen_buffer(KNI_FALSE);
    clearScreen();
}

void resizeScreenBuffer(int width, int height) {
    if (gxj_resize_screen_buffer(width, height) != ALL_OK) {
        abort();
    }

    pthread_mutex_lock(&portMutex);
    if (!ensure_frame_buffer_locked(width, height)) {
        pthread_mutex_unlock(&portMutex);
        abort();
    }
    pthread_mutex_unlock(&portMutex);
}

void clearScreen(void) {
    pthread_mutex_lock(&portMutex);
    if (framePixels != NULL) {
        memset(
            framePixels,
            0,
            (size_t)frameWidth * (size_t)frameHeight * sizeof(gxj_pixel_type)
        );
    }
    pthread_mutex_unlock(&portMutex);
}

int getScreenX(int screenRotated) {
    (void)screenRotated;
    return 0;
}

int getScreenY(int screenRotated) {
    (void)screenRotated;
    return 0;
}

void refreshScreenNormal(int x1, int y1, int x2, int y2) {
    const gxj_pixel_type* source = gxj_system_screen_buffer.pixelData;
    int width = gxj_system_screen_buffer.width;
    int height = gxj_system_screen_buffer.height;
    int y;

    if (source == NULL ||
            !clip_rectangle(&x1, &y1, &x2, &y2, width, height)) {
        return;
    }

    pthread_mutex_lock(&portMutex);
    if (!ensure_frame_buffer_locked(width, height)) {
        pthread_mutex_unlock(&portMutex);
        return;
    }

    for (y = y1; y < y2; ++y) {
        memcpy(
            framePixels + y * frameWidth + x1,
            source + y * width + x1,
            (size_t)(x2 - x1) * sizeof(gxj_pixel_type)
        );
    }
    pthread_mutex_unlock(&portMutex);
}

void refreshScreenRotated(int x1, int y1, int x2, int y2) {
    const gxj_pixel_type* source = gxj_system_screen_buffer.pixelData;
    int width = gxj_system_screen_buffer.width;
    int height = gxj_system_screen_buffer.height;
    int x;
    int y;

    if (source == NULL ||
            !clip_rectangle(&x1, &y1, &x2, &y2, width, height)) {
        return;
    }

    pthread_mutex_lock(&portMutex);
    if (!ensure_frame_buffer_locked(height, width)) {
        pthread_mutex_unlock(&portMutex);
        return;
    }

    for (y = y1; y < y2; ++y) {
        for (x = x1; x < x2; ++x) {
            int rotatedX = y;
            int rotatedY = width - x - 1;
            framePixels[rotatedY * frameWidth + rotatedX] =
                source[y * width + x];
        }
    }
    pthread_mutex_unlock(&portMutex);
}

void finalizeFrameBuffer(void) {
    pthread_mutex_lock(&portMutex);
    free(framePixels);
    framePixels = NULL;
    frameWidth = 0;
    frameHeight = 0;
    pthread_mutex_unlock(&portMutex);
    gxj_free_screen_buffer();
}
