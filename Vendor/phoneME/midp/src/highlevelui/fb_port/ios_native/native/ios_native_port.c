/*
 * Native in-process framebuffer and event queues for the iOS phoneME host.
 * LCDUI renders into its RGB565 software buffer; the Swift host reads a stable
 * RGBA8888 snapshot through the small C ABI.
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

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
/*
 * Host frame conversion is serialized separately so the VM only holds
 * portMutex for a fast RGB565 snapshot copy, not for the more expensive
 * RGB565 -> RGBA conversion.
 */
static pthread_mutex_t frameCopyMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t wakePipeOnce = PTHREAD_ONCE_INIT;
static int wakeReadFD = -1;
static int wakeWriteFD = -1;
static gxj_pixel_type* framePixels;
static gxj_pixel_type* frameSnapshotPixels;
static size_t frameSnapshotCapacity;
static int frameWidth;
static int frameHeight;
static uint64_t frameGeneration;
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

static void initialize_wake_pipe(void) {
    int descriptors[2];
    int flags;

    if (pipe(descriptors) != 0) {
        return;
    }

    flags = fcntl(descriptors[0], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK);
    }
    flags = fcntl(descriptors[1], F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(descriptors[1], F_SETFL, flags | O_NONBLOCK);
    }
    (void)fcntl(descriptors[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(descriptors[1], F_SETFD, FD_CLOEXEC);

    wakeReadFD = descriptors[0];
    wakeWriteFD = descriptors[1];
}

static void signal_event_waiter(void) {
    uint8_t marker = 1U;
    ssize_t result;

    (void)pthread_once(&wakePipeOnce, initialize_wake_pipe);
    if (wakeWriteFD < 0) {
        return;
    }

    do {
        result = write(wakeWriteFD, &marker, sizeof(marker));
    } while (result < 0 && errno == EINTR);
    /* EAGAIN only means the pipe is already readable, which is sufficient. */
}

void phoneme_ios_port_drain_wakeup(void) {
    uint8_t buffer[64];
    ssize_t result;

    (void)pthread_once(&wakePipeOnce, initialize_wake_pipe);
    if (wakeReadFD < 0) {
        return;
    }

    do {
        result = read(wakeReadFD, buffer, sizeof(buffer));
    } while (result > 0 || (result < 0 && errno == EINTR));
}

static void advance_frame_generation_locked(void) {
    ++frameGeneration;
    if (frameGeneration == 0U) {
        ++frameGeneration;
    }
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
    advance_frame_generation_locked();
    return 1;
}

static uint8_t expand5(unsigned int value) {
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t expand6(unsigned int value) {
    return (uint8_t)((value << 2) | (value >> 4));
}

static void convert_rgb565_to_rgba(
        const gxj_pixel_type* source,
        uint8_t* destination,
        size_t pixelCount) {
    size_t index = 0;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const uint16x8_t mask5 = vdupq_n_u16(0x1fU);
    const uint16x8_t mask6 = vdupq_n_u16(0x3fU);
    const uint8x8_t alpha = vdup_n_u8(0xffU);

    for (; index + 8U <= pixelCount; index += 8U) {
        const uint16x8_t pixels = vld1q_u16(
            (const uint16_t*)(source + index)
        );
        const uint16x8_t red5 = vshrq_n_u16(pixels, 11);
        const uint16x8_t green6 = vandq_u16(
            vshrq_n_u16(pixels, 5),
            mask6
        );
        const uint16x8_t blue5 = vandq_u16(pixels, mask5);
        uint8x8x4_t rgba;

        rgba.val[0] = vmovn_u16(vorrq_u16(
            vshlq_n_u16(red5, 3),
            vshrq_n_u16(red5, 2)
        ));
        rgba.val[1] = vmovn_u16(vorrq_u16(
            vshlq_n_u16(green6, 2),
            vshrq_n_u16(green6, 4)
        ));
        rgba.val[2] = vmovn_u16(vorrq_u16(
            vshlq_n_u16(blue5, 3),
            vshrq_n_u16(blue5, 2)
        ));
        rgba.val[3] = alpha;
        vst4_u8(destination + index * 4U, rgba);
    }
#endif

    for (; index < pixelCount; ++index) {
        unsigned int pixel = (unsigned int)source[index];
        destination[index * 4U + 0U] = expand5((pixel >> 11) & 0x1fU);
        destination[index * 4U + 1U] = expand6((pixel >> 5) & 0x3fU);
        destination[index * 4U + 2U] = expand5(pixel & 0x1fU);
        destination[index * 4U + 3U] = 0xffU;
    }
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
    phoneme_ios_port_drain_wakeup();
    pthread_mutex_lock(&portMutex);
    if (framePixels != NULL) {
        memset(
            framePixels,
            0,
            (size_t)frameWidth * (size_t)frameHeight * sizeof(gxj_pixel_type)
        );
        advance_frame_generation_locked();
    }
    keyReadIndex = 0;
    keyWriteIndex = 0;
    pointerReadIndex = 0;
    pointerWriteIndex = 0;
    __atomic_store_n(&stopRequested, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&portMutex);
}

void phoneme_ios_port_request_stop(void) {
    /*
     * This flag is read both from the MIDP event loop and directly from the
     * CLDC interpreter. Keep it lock-free so a host-side stop request cannot
     * wait behind a VM thread that happens to be holding portMutex.
     */
    __atomic_store_n(&stopRequested, 1, __ATOMIC_RELEASE);
    signal_event_waiter();
}

int phoneme_ios_port_should_stop(void) {
    return __atomic_load_n(&stopRequested, __ATOMIC_ACQUIRE);
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
    signal_event_waiter();
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
    signal_event_waiter();
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
        int32_t* height,
        uint64_t* generation) {
    int32_t required;
    size_t pixelCount;

    pthread_mutex_lock(&frameCopyMutex);

    for (;;) {
        gxj_pixel_type* replacement;

        pthread_mutex_lock(&portMutex);
        if (width != NULL) *width = frameWidth;
        if (height != NULL) *height = frameHeight;
        if (generation != NULL) *generation = frameGeneration;

        if (framePixels == NULL || frameWidth <= 0 || frameHeight <= 0 ||
                (size_t)frameWidth * (size_t)frameHeight > INT32_MAX / 4) {
            pthread_mutex_unlock(&portMutex);
            pthread_mutex_unlock(&frameCopyMutex);
            return 0;
        }

        pixelCount = (size_t)frameWidth * (size_t)frameHeight;
        required = (int32_t)(pixelCount * 4U);
        if (destination == NULL || capacity < required) {
            pthread_mutex_unlock(&portMutex);
            pthread_mutex_unlock(&frameCopyMutex);
            return required;
        }

        if (frameSnapshotCapacity >= pixelCount) {
            memcpy(
                frameSnapshotPixels,
                framePixels,
                pixelCount * sizeof(gxj_pixel_type)
            );
            pthread_mutex_unlock(&portMutex);
            break;
        }
        pthread_mutex_unlock(&portMutex);

        replacement = (gxj_pixel_type*)realloc(
            frameSnapshotPixels,
            pixelCount * sizeof(gxj_pixel_type)
        );
        if (replacement == NULL) {
            pthread_mutex_unlock(&frameCopyMutex);
            return 0;
        }
        frameSnapshotPixels = replacement;
        frameSnapshotCapacity = pixelCount;
        /* Re-read dimensions and pixels after allocation in case of resize. */
    }

    convert_rgb565_to_rgba(
        frameSnapshotPixels,
        destination,
        pixelCount
    );
    pthread_mutex_unlock(&frameCopyMutex);
    return required;
}

int getKeyboardFd(void) {
    (void)pthread_once(&wakePipeOnce, initialize_wake_pipe);
    return wakeReadFD;
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
        advance_frame_generation_locked();
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
    advance_frame_generation_locked();
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
    advance_frame_generation_locked();
    pthread_mutex_unlock(&portMutex);
}

void finalizeFrameBuffer(void) {
    pthread_mutex_lock(&frameCopyMutex);
    pthread_mutex_lock(&portMutex);
    free(framePixels);
    framePixels = NULL;
    frameWidth = 0;
    frameHeight = 0;
    pthread_mutex_unlock(&portMutex);

    free(frameSnapshotPixels);
    frameSnapshotPixels = NULL;
    frameSnapshotCapacity = 0;
    pthread_mutex_unlock(&frameCopyMutex);
    gxj_free_screen_buffer();
}
