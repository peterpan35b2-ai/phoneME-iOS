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
#include <midpMalloc.h>
#include <midpServices.h>
#include <midp_global_status.h>

#include "ios_native_port.h"

#define PHONEME_IOS_QUEUE_SIZE 128
#define PHONEME_IOS_DISPLAY_SLOTS 128
#define PHONEME_IOS_PRESSED_KEY_CAPACITY 32

typedef struct {
    int32_t key_code;
    int32_t pressed;
} PhoneMEIOSKeyEvent;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t action;
} PhoneMEIOSPointerEvent;

typedef struct {
    int isolateId;
    gxj_screen_buffer screen;
    int configuredWidth;
    int configuredHeight;
    int reverseOrientation;
    int fullScreenMode;
} PhoneMEIOSDisplayContext;

static PhoneMEIOSDisplayContext displayContexts[PHONEME_IOS_DISPLAY_SLOTS];
static pthread_mutex_t displayRegistryMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t portMutex = PTHREAD_MUTEX_INITIALIZER;
static __thread int cachedDisplayIsolateId = INT_MIN;
static __thread PhoneMEIOSDisplayContext* cachedDisplayContext;
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
static int pendingConfiguredWidth = 240;
static int pendingConfiguredHeight = 320;
static int foregroundIsolateId;
static int foregroundTransitionActive;
static PhoneMEIOSKeyEvent keyQueue[PHONEME_IOS_QUEUE_SIZE];
static unsigned int keyReadIndex;
static unsigned int keyWriteIndex;
static PhoneMEIOSPointerEvent pointerQueue[PHONEME_IOS_QUEUE_SIZE];
static unsigned int pointerReadIndex;
static unsigned int pointerWriteIndex;
static int32_t pressedKeys[PHONEME_IOS_PRESSED_KEY_CAPACITY];
static int pressedKeyCount;
static int pointerActive;
static int32_t activePointerX;
static int32_t activePointerY;
static int stopRequested;

static unsigned int queue_next(unsigned int index) {
    return (index + 1U) % PHONEME_IOS_QUEUE_SIZE;
}

static void update_pressed_key_locked(int32_t keyCode, int pressed) {
    int index;
    for (index = 0; index < pressedKeyCount; ++index) {
        if (pressedKeys[index] == keyCode) {
            if (!pressed) {
                pressedKeys[index] = pressedKeys[pressedKeyCount - 1];
                --pressedKeyCount;
            }
            return;
        }
    }
    if (pressed && pressedKeyCount < PHONEME_IOS_PRESSED_KEY_CAPACITY) {
        pressedKeys[pressedKeyCount++] = keyCode;
    }
}

static PhoneMEIOSDisplayContext* find_display_context_locked(
        int isolateId,
        int create) {
    PhoneMEIOSDisplayContext* empty = NULL;
    int index;

    if (isolateId <= 0) {
        displayContexts[0].isolateId = 0;
        return &displayContexts[0];
    }

    for (index = 1; index < PHONEME_IOS_DISPLAY_SLOTS; ++index) {
        PhoneMEIOSDisplayContext* context = &displayContexts[index];
        if (context->isolateId == isolateId) {
            return context;
        }
        if (empty == NULL && context->isolateId == 0) {
            empty = context;
        }
    }
    if (create && empty != NULL) {
        memset(empty, 0, sizeof(*empty));
        empty->isolateId = isolateId;
        return empty;
    }
    return NULL;
}

static PhoneMEIOSDisplayContext* display_context_for_isolate(int isolateId) {
    PhoneMEIOSDisplayContext* context;
    pthread_mutex_lock(&displayRegistryMutex);
    context = find_display_context_locked(isolateId, 1);
    pthread_mutex_unlock(&displayRegistryMutex);
    if (context == NULL) {
        abort();
    }
    return context;
}

static PhoneMEIOSDisplayContext* current_display_context(void) {
    int isolateId = getCurrentIsolateId();
    if (cachedDisplayContext != NULL &&
            cachedDisplayIsolateId == isolateId &&
            (isolateId <= 0 || cachedDisplayContext->isolateId == isolateId)) {
        return cachedDisplayContext;
    }
    cachedDisplayContext = display_context_for_isolate(isolateId);
    cachedDisplayIsolateId = isolateId;
    return cachedDisplayContext;
}

gxj_screen_buffer* phoneme_ios_current_screen_buffer(void) {
    return &current_display_context()->screen;
}

static void free_display_context_locked(PhoneMEIOSDisplayContext* context) {
    if (context == NULL) {
        return;
    }
    if (context->screen.pixelData != NULL) {
        midpFree(context->screen.pixelData);
    }
    if (context->screen.alphaData != NULL) {
        midpFree(context->screen.alphaData);
    }
    memset(context, 0, sizeof(*context));
}

/* portMutex must already be held. Registry locking keeps slot reuse from
 * racing a native graphics lookup on another VM thread. */
static void free_display_context_for_isolate_locked(int isolateId) {
    PhoneMEIOSDisplayContext* context;
    pthread_mutex_lock(&displayRegistryMutex);
    context = find_display_context_locked(isolateId, 0);
    free_display_context_locked(context);
    pthread_mutex_unlock(&displayRegistryMutex);
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

static void publish_display_context_locked(
        const PhoneMEIOSDisplayContext* context) {
    size_t pixelCount;

    if (context == NULL || context->screen.pixelData == NULL ||
            context->screen.width <= 0 || context->screen.height <= 0) {
        if (framePixels != NULL && frameWidth > 0 && frameHeight > 0) {
            memset(
                framePixels,
                0,
                (size_t)frameWidth * (size_t)frameHeight *
                    sizeof(gxj_pixel_type)
            );
            advance_frame_generation_locked();
        }
        return;
    }

    if (!ensure_frame_buffer_locked(
            context->screen.width,
            context->screen.height)) {
        return;
    }
    pixelCount = (size_t)context->screen.width *
        (size_t)context->screen.height;
    memcpy(
        framePixels,
        context->screen.pixelData,
        pixelCount * sizeof(gxj_pixel_type)
    );
    advance_frame_generation_locked();
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
    pendingConfiguredWidth = (int)width;
    pendingConfiguredHeight = (int)height;
    pthread_mutex_unlock(&portMutex);
}

void phoneme_ios_port_set_isolate_display(
        int32_t isolateId,
        int32_t width,
        int32_t height) {
    PhoneMEIOSDisplayContext* context;
    if (isolateId <= 0 || width < 1 || height < 1 ||
            width > 2048 || height > 2048) {
        return;
    }
    pthread_mutex_lock(&portMutex);
    context = display_context_for_isolate(isolateId);
    context->configuredWidth = (int)width;
    context->configuredHeight = (int)height;
    pthread_mutex_unlock(&portMutex);
}

int32_t phoneme_ios_port_configured_width(void) {
    PhoneMEIOSDisplayContext* context = current_display_context();
    int32_t result;
    pthread_mutex_lock(&portMutex);
    result = context->configuredWidth > 0
        ? (int32_t)context->configuredWidth
        : (int32_t)pendingConfiguredWidth;
    pthread_mutex_unlock(&portMutex);
    return result;
}

int32_t phoneme_ios_port_configured_height(void) {
    PhoneMEIOSDisplayContext* context = current_display_context();
    int32_t result;
    pthread_mutex_lock(&portMutex);
    result = context->configuredHeight > 0
        ? (int32_t)context->configuredHeight
        : (int32_t)pendingConfiguredHeight;
    pthread_mutex_unlock(&portMutex);
    return result;
}

int32_t phoneme_ios_port_reverse_orientation(void) {
    return current_display_context()->reverseOrientation;
}

int32_t phoneme_ios_port_toggle_reverse_orientation(void) {
    PhoneMEIOSDisplayContext* context = current_display_context();
    context->reverseOrientation = !context->reverseOrientation;
    return context->reverseOrientation;
}

int32_t phoneme_ios_port_fullscreen_mode(void) {
    return current_display_context()->fullScreenMode;
}

void phoneme_ios_port_set_fullscreen_mode(int32_t mode) {
    current_display_context()->fullScreenMode = mode != 0;
}

void phoneme_ios_port_reset_current_display_state(void) {
    PhoneMEIOSDisplayContext* context = current_display_context();
    context->reverseOrientation = 0;
    context->fullScreenMode = 0;
}

void phoneme_ios_port_prepare_foreground(int32_t isolateId) {
    (void)isolateId;
    pthread_mutex_lock(&portMutex);
    __atomic_store_n(&foregroundTransitionActive, 1, __ATOMIC_RELEASE);
    __atomic_store_n(&foregroundIsolateId, 0, __ATOMIC_RELEASE);
    keyReadIndex = keyWriteIndex = 0;
    pointerReadIndex = pointerWriteIndex = 0;
    if (framePixels != NULL && frameWidth > 0 && frameHeight > 0) {
        memset(
            framePixels,
            0,
            (size_t)frameWidth * (size_t)frameHeight * sizeof(gxj_pixel_type)
        );
        advance_frame_generation_locked();
    }
    pthread_mutex_unlock(&portMutex);
}

void phoneme_ios_port_commit_foreground(int32_t isolateId) {
    pthread_mutex_lock(&portMutex);
    __atomic_store_n(&foregroundIsolateId, isolateId, __ATOMIC_RELEASE);
    __atomic_store_n(&foregroundTransitionActive, 0, __ATOMIC_RELEASE);
    publish_display_context_locked(
        isolateId > 0 ? display_context_for_isolate(isolateId) : NULL
    );
    pthread_mutex_unlock(&portMutex);
}

void phoneme_ios_port_release_isolate(int32_t isolateId) {
    int wasForeground;
    if (isolateId <= 0) {
        return;
    }
    pthread_mutex_lock(&frameCopyMutex);
    pthread_mutex_lock(&portMutex);
    wasForeground = isolateId ==
        __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE);
    free_display_context_for_isolate_locked(isolateId);
    if (wasForeground) {
        __atomic_store_n(&foregroundIsolateId, 0, __ATOMIC_RELEASE);
        publish_display_context_locked(NULL);
    }
    pthread_mutex_unlock(&portMutex);
    pthread_mutex_unlock(&frameCopyMutex);
}

void phoneme_ios_port_reset(void) {
    int index;
    phoneme_ios_port_drain_wakeup();
    pthread_mutex_lock(&frameCopyMutex);
    pthread_mutex_lock(&portMutex);
    pthread_mutex_lock(&displayRegistryMutex);
    for (index = 0; index < PHONEME_IOS_DISPLAY_SLOTS; ++index) {
        free_display_context_locked(&displayContexts[index]);
    }
    pthread_mutex_unlock(&displayRegistryMutex);
    free(framePixels);
    framePixels = NULL;
    frameWidth = 0;
    frameHeight = 0;
    free(frameSnapshotPixels);
    frameSnapshotPixels = NULL;
    frameSnapshotCapacity = 0;
    frameGeneration = 0;
    foregroundIsolateId = 0;
    foregroundTransitionActive = 0;
    keyReadIndex = keyWriteIndex = 0;
    pointerReadIndex = pointerWriteIndex = 0;
    pressedKeyCount = 0;
    pointerActive = 0;
    activePointerX = 0;
    activePointerY = 0;
    __atomic_store_n(&stopRequested, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&portMutex);
    pthread_mutex_unlock(&frameCopyMutex);
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
    if (__atomic_load_n(&foregroundTransitionActive, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE) <= 0) {
        pthread_mutex_unlock(&portMutex);
        return;
    }
    update_pressed_key_locked(key_code, pressed != 0);
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

int32_t phoneme_ios_port_take_pressed_keys(
        int32_t* destination,
        int32_t capacity) {
    int count;
    int copied;

    pthread_mutex_lock(&portMutex);
    count = pressedKeyCount;
    copied = capacity < count ? capacity : count;
    if (destination != NULL && copied > 0) {
        memcpy(destination, pressedKeys, (size_t)copied * sizeof(int32_t));
    }
    pressedKeyCount = 0;
    pthread_mutex_unlock(&portMutex);
    return copied;
}

void phoneme_ios_port_send_pointer(
        int32_t x, int32_t y, int32_t action) {
    unsigned int next;

    pthread_mutex_lock(&portMutex);
    if (__atomic_load_n(&foregroundTransitionActive, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE) <= 0 ||
            frameWidth <= 0 || frameHeight <= 0) {
        pthread_mutex_unlock(&portMutex);
        return;
    }

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= frameWidth) x = frameWidth - 1;
    if (y >= frameHeight) y = frameHeight - 1;

    if (action == 1 || action == 3) {
        pointerActive = 1;
        activePointerX = x;
        activePointerY = y;
    } else if (action == 2) {
        pointerActive = 0;
        activePointerX = x;
        activePointerY = y;
    }

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

int32_t phoneme_ios_port_take_active_pointer(int32_t* x, int32_t* y) {
    int result;
    pthread_mutex_lock(&portMutex);
    result = pointerActive;
    if (result) {
        if (x != NULL) *x = activePointerX;
        if (y != NULL) *y = activePointerY;
    }
    pointerActive = 0;
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
    PhoneMEIOSDisplayContext* context = current_display_context();
    int isolateId = getCurrentIsolateId();

    if (gxj_init_screen_buffer(width, height) != ALL_OK) {
        abort();
    }

    pthread_mutex_lock(&portMutex);
    if (context->configuredWidth <= 0 || context->configuredHeight <= 0) {
        context->configuredWidth = width;
        context->configuredHeight = height;
    }
    if (isolateId ==
            __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE) &&
            !ensure_frame_buffer_locked(width, height)) {
        pthread_mutex_unlock(&portMutex);
        abort();
    }
    pthread_mutex_unlock(&portMutex);
}

void connectFrameBuffer(void) {
    /* PhoneMECore owns runtime-global reset. Per-isolate initialization must
     * not erase another MIDlet's framebuffer or input queue. */
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
    if (getCurrentIsolateId() ==
            __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE) &&
            !ensure_frame_buffer_locked(width, height)) {
        pthread_mutex_unlock(&portMutex);
        abort();
    }
    pthread_mutex_unlock(&portMutex);
}

void clearScreen(void) {
    pthread_mutex_lock(&portMutex);
    if (getCurrentIsolateId() ==
            __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE) &&
            framePixels != NULL) {
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
            getCurrentIsolateId() !=
                __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&foregroundTransitionActive, __ATOMIC_ACQUIRE) ||
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
            getCurrentIsolateId() !=
                __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&foregroundTransitionActive, __ATOMIC_ACQUIRE) ||
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
    int isolateId = getCurrentIsolateId();
    int wasForeground;

    pthread_mutex_lock(&frameCopyMutex);
    pthread_mutex_lock(&portMutex);
    wasForeground = isolateId ==
        __atomic_load_n(&foregroundIsolateId, __ATOMIC_ACQUIRE);
    free_display_context_for_isolate_locked(isolateId);
    if (wasForeground) {
        __atomic_store_n(&foregroundIsolateId, 0, __ATOMIC_RELEASE);
        if (framePixels != NULL && frameWidth > 0 && frameHeight > 0) {
            memset(
                framePixels,
                0,
                (size_t)frameWidth * (size_t)frameHeight *
                    sizeof(gxj_pixel_type)
            );
            advance_frame_generation_locked();
        }
    }
    pthread_mutex_unlock(&portMutex);
    pthread_mutex_unlock(&frameCopyMutex);
}
