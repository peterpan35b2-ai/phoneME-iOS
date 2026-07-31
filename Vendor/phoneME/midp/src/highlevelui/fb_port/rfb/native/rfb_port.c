/*
 * Headless RGB565 framebuffer backend for phoneME using LibVNCServer.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rfb/rfb.h>

#include <fbport_export.h>
#include <gxj_putpixel.h>
#include <gxj_screen_buffer.h>
#include <midp_global_status.h>

#include "rfb_port.h"

#define PHONEME_RFB_WIDTH  320
#define PHONEME_RFB_HEIGHT 240
#define PHONEME_RFB_DEFAULT_PORT 5900
#define PHONEME_RFB_DEFAULT_DEFER_MS 40

/** System offscreen buffer used by LCDUI. */
gxj_screen_buffer gxj_system_screen_buffer;

static rfbScreenInfoPtr rfbScreen = NULL;
static gxj_pixel_type* rfbPixels = NULL;
static int keyboardPipe[2] = {-1, -1};
static int pointerPipe[2] = {-1, -1};
static char* passwordStorage = NULL;

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

static void set_close_on_exec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static void close_pipe_pair(int fds[2]) {
    if (fds[0] >= 0) {
        close(fds[0]);
        fds[0] = -1;
    }
    if (fds[1] >= 0) {
        close(fds[1]);
        fds[1] = -1;
    }
}

static int create_input_pipes(void) {
    if (pipe(keyboardPipe) != 0) {
        perror("phoneME RFB keyboard pipe");
        return 0;
    }
    if (pipe(pointerPipe) != 0) {
        perror("phoneME RFB pointer pipe");
        close_pipe_pair(keyboardPipe);
        return 0;
    }

    set_close_on_exec(keyboardPipe[0]);
    set_close_on_exec(keyboardPipe[1]);
    set_close_on_exec(pointerPipe[0]);
    set_close_on_exec(pointerPipe[1]);

    /* select() guards reads; nonblocking mode also makes shutdown safe. */
    set_nonblocking(keyboardPipe[0]);
    set_nonblocking(pointerPipe[0]);
    return 1;
}

static void write_key_event(const PhoneMeRfbKeyEvent* event) {
    ssize_t written;

    if (keyboardPipe[1] < 0) {
        return;
    }

    do {
        written = write(keyboardPipe[1], event, sizeof(*event));
    } while (written < 0 && errno == EINTR);
}

static void vnc_keyboard_event(rfbBool down, rfbKeySym keySym,
        rfbClientPtr client) {
    PhoneMeRfbKeyEvent event;
    (void)client;

    event.keysym = (unsigned int)keySym;
    event.pressed = down ? 1 : 0;
    write_key_event(&event);
}

static void vnc_pointer_event(int buttonMask, int x, int y,
        rfbClientPtr client) {
    PhoneMeRfbPointerEvent event;
    ssize_t written;
    (void)client;

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= PHONEME_RFB_WIDTH) x = PHONEME_RFB_WIDTH - 1;
    if (y >= PHONEME_RFB_HEIGHT) y = PHONEME_RFB_HEIGHT - 1;

    if (pointerPipe[1] < 0) {
        return;
    }

    event.x = x;
    event.y = y;
    event.buttonMask = buttonMask;

    do {
        written = write(pointerPipe[1], &event, sizeof(event));
    } while (written < 0 && errno == EINTR);
}

static int host_is_big_endian(void) {
    const unsigned short value = 0x0102;
    return *((const unsigned char*)&value) == 0x01;
}

static void configure_rgb565_format(void) {
    rfbPixelFormat* format = &rfbScreen->serverFormat;

    format->bitsPerPixel = 16;
    format->depth = 16;
    format->bigEndian = host_is_big_endian() ? TRUE : FALSE;
    format->trueColour = TRUE;
    format->redMax = 31;
    format->greenMax = 63;
    format->blueMax = 31;
    format->redShift = 11;
    format->greenShift = 5;
    format->blueShift = 0;
}

static rfbBool check_rfb_password(rfbClientPtr client,
        const char* response, int length) {
    unsigned char encrypted[CHALLENGESIZE];

    if (client == NULL || response == NULL || passwordStorage == NULL ||
            length != CHALLENGESIZE) {
        return FALSE;
    }

    memcpy(encrypted, client->authChallenge, CHALLENGESIZE);
    rfbEncryptBytes(encrypted, passwordStorage);
    return memcmp(encrypted, response, CHALLENGESIZE) == 0 ? TRUE : FALSE;
}

static int configure_password(void) {
    const char* password = getenv("PHONEME_RFB_PASSWORD");
    size_t length;

    if (password == NULL || *password == '\0') {
        fprintf(stderr,
            "Warning: phoneME RFB has no password; use LAN/VPN or set "
            "PHONEME_RFB_PASSWORD.\n");
        return 1;
    }

    /* Classic VNC authentication uses only the first eight characters. */
    length = strlen(password);
    if (length > 8) {
        length = 8;
    }

    passwordStorage = (char*)malloc(length + 1);
    if (passwordStorage == NULL) {
        return 0;
    }
    memcpy(passwordStorage, password, length);
    passwordStorage[length] = '\0';

    rfbScreen->authPasswdData = passwordStorage;
    rfbScreen->passwordCheck = check_rfb_password;
    return 1;
}

static int start_rfb_server(void) {
    int argc = 1;
    char programName[] = "phoneme-rfb";
    char* argvStorage[2];
    int port;
    int deferMs;

    argvStorage[0] = programName;
    argvStorage[1] = NULL;

    rfbPixels = (gxj_pixel_type*)calloc(
        (size_t)PHONEME_RFB_WIDTH * PHONEME_RFB_HEIGHT,
        sizeof(gxj_pixel_type));
    if (rfbPixels == NULL) {
        fprintf(stderr, "Failed to allocate phoneME RFB framebuffer\n");
        return 0;
    }

    rfbScreen = rfbGetScreen(&argc, argvStorage,
        PHONEME_RFB_WIDTH, PHONEME_RFB_HEIGHT, 5, 3, 2);
    if (rfbScreen == NULL) {
        fprintf(stderr, "Failed to initialize LibVNCServer\n");
        free(rfbPixels);
        rfbPixels = NULL;
        return 0;
    }

    port = parse_env_int("PHONEME_RFB_PORT",
        PHONEME_RFB_DEFAULT_PORT, 1, 65535);
    deferMs = parse_env_int("PHONEME_RFB_DEFER_MS",
        PHONEME_RFB_DEFAULT_DEFER_MS, 0, 1000);

    rfbScreen->frameBuffer = (char*)rfbPixels;
    rfbScreen->desktopName = "phoneME 320x240";
    rfbScreen->port = port;
    rfbScreen->ipv6port = port;
    rfbScreen->deferUpdateTime = deferMs;
    rfbScreen->deferPtrUpdateTime = 16;
    rfbScreen->alwaysShared = TRUE;
    rfbScreen->kbdAddEvent = vnc_keyboard_event;
    rfbScreen->ptrAddEvent = vnc_pointer_event;
    configure_rgb565_format();

    if (!configure_password()) {
        fprintf(stderr, "Failed to allocate RFB password storage\n");
        rfbScreenCleanup(rfbScreen);
        rfbScreen = NULL;
        free(rfbPixels);
        rfbPixels = NULL;
        return 0;
    }

    rfbInitServer(rfbScreen);
    /* Socket activity wakes select() immediately; one second keeps idle CPU low. */
    rfbRunEventLoop(rfbScreen, 1000000, TRUE);

    fprintf(stderr,
        "phoneME headless RFB ready: 320x240 RGB565, port %d, defer %d ms\n",
        port, deferMs);
    return 1;
}

int phoneme_rfb_read_key_event(PhoneMeRfbKeyEvent* event) {
    ssize_t result;

    if (event == NULL || keyboardPipe[0] < 0) {
        return 0;
    }

    do {
        result = read(keyboardPipe[0], event, sizeof(*event));
    } while (result < 0 && errno == EINTR);

    return result == (ssize_t)sizeof(*event);
}

int phoneme_rfb_read_pointer_event(PhoneMeRfbPointerEvent* event) {
    ssize_t result;

    if (event == NULL || pointerPipe[0] < 0) {
        return 0;
    }

    do {
        result = read(pointerPipe[0], event, sizeof(*event));
    } while (result < 0 && errno == EINTR);

    return result == (ssize_t)sizeof(*event);
}

int getKeyboardFd(void) {
    return keyboardPipe[0];
}

int getMouseFd(void) {
    return pointerPipe[0];
}

void initScreenBuffer(int width, int height) {
    if (gxj_init_screen_buffer(width, height) != ALL_OK) {
        fprintf(stderr, "Failed to allocate phoneME screen buffer\n");
        exit(1);
    }
}

void connectFrameBuffer(void) {
    if (!create_input_pipes() || !start_rfb_server()) {
        close_pipe_pair(keyboardPipe);
        close_pipe_pair(pointerPipe);
        exit(1);
    }
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
    if (rfbPixels != NULL) {
        memset(rfbPixels, 0,
            (size_t)PHONEME_RFB_WIDTH * PHONEME_RFB_HEIGHT *
            sizeof(gxj_pixel_type));
        if (rfbScreen != NULL) {
            rfbMarkRectAsModified(rfbScreen, 0, 0,
                PHONEME_RFB_WIDTH, PHONEME_RFB_HEIGHT);
        }
    }
}

int getScreenX(int screenRotated) {
    int lcdWidth = screenRotated ? PHONEME_RFB_HEIGHT : PHONEME_RFB_WIDTH;
    int width = gxj_system_screen_buffer.width;
    return lcdWidth > width ? (lcdWidth - width) / 2 : 0;
}

int getScreenY(int screenRotated) {
    int lcdHeight = screenRotated ? PHONEME_RFB_WIDTH : PHONEME_RFB_HEIGHT;
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

    if (rfbScreen == NULL || rfbPixels == NULL || src == NULL) {
        return;
    }
    if (width > PHONEME_RFB_WIDTH || height > PHONEME_RFB_HEIGHT) {
        fprintf(stderr, "phoneME screen %dx%d exceeds RFB 320x240\n",
            width, height);
        return;
    }
    if (!clip_rectangle(&x1, &y1, &x2, &y2, width, height)) {
        return;
    }

    dstX = (PHONEME_RFB_WIDTH - width) / 2;
    dstY = (PHONEME_RFB_HEIGHT - height) / 2;

    for (y = y1; y < y2; ++y) {
        memcpy(rfbPixels + (dstY + y) * PHONEME_RFB_WIDTH + dstX + x1,
            src + y * width + x1,
            (size_t)(x2 - x1) * sizeof(gxj_pixel_type));
    }

    rfbMarkRectAsModified(rfbScreen,
        dstX + x1, dstY + y1, dstX + x2, dstY + y2);
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

    if (rfbScreen == NULL || rfbPixels == NULL || src == NULL) {
        return;
    }
    if (outputWidth > PHONEME_RFB_WIDTH ||
            outputHeight > PHONEME_RFB_HEIGHT) {
        fprintf(stderr,
            "Rotated phoneME screen %dx%d exceeds RFB 320x240\n",
            outputWidth, outputHeight);
        return;
    }
    if (!clip_rectangle(&x1, &y1, &x2, &y2, width, height)) {
        return;
    }

    dstX = (PHONEME_RFB_WIDTH - outputWidth) / 2;
    dstY = (PHONEME_RFB_HEIGHT - outputHeight) / 2;

    for (y = y1; y < y2; ++y) {
        for (x = x1; x < x2; ++x) {
            int rotatedX = y;
            int rotatedY = width - x - 1;
            rfbPixels[(dstY + rotatedY) * PHONEME_RFB_WIDTH +
                dstX + rotatedX] = src[y * width + x];
        }
    }

    rfbMarkRectAsModified(rfbScreen,
        dstX + y1,
        dstY + width - x2,
        dstX + y2,
        dstY + width - x1);
}

void finalizeFrameBuffer(void) {
    if (rfbScreen != NULL) {
        /*
         * Some LibVNCServer releases mark HTTP initialized even when httpDir
         * is NULL, then try to destroy uninitialized HTTP locks on shutdown.
         */
        if (rfbScreen->httpDir == NULL) {
            rfbScreen->httpInitDone = FALSE;
        }
        rfbShutdownServer(rfbScreen, TRUE);
        rfbScreenCleanup(rfbScreen);
        rfbScreen = NULL;
    }

    close_pipe_pair(keyboardPipe);
    close_pipe_pair(pointerPipe);

    free(passwordStorage);
    passwordStorage = NULL;

    free(rfbPixels);
    rfbPixels = NULL;
    gxj_free_screen_buffer();
}
