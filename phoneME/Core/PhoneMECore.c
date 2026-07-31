#include "PhoneMECore.h"

#include <pthread.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <TargetConditionals.h>

#if TARGET_OS_IOS

/* Exported by the minimally selected phoneME MIDP objects. */
extern int runMidlet(int argc, char** commandlineArgs);
extern void phoneme_ios_port_configure_display(
    int32_t width,
    int32_t height
);
extern void phoneme_ios_port_reset(void);
extern void phoneme_ios_port_request_stop(void);
extern int phoneme_ios_port_should_stop(void);
typedef int (*PhoneMEHostShouldStopCallback)(void);
extern void phoneme_cldc_set_should_stop_callback(
    PhoneMEHostShouldStopCallback callback
);
extern void phoneme_ios_port_send_key(int32_t key_code, int32_t pressed);
extern void phoneme_ios_keymap_configure(
    int up,
    int down,
    int left,
    int right,
    int fire,
    int soft1,
    int soft2
);
extern int phoneme_ios_keymap_convert_key_code(int key_code);
extern void phoneme_ios_port_send_pointer(
    int32_t x,
    int32_t y,
    int32_t action
);
extern int32_t phoneme_ios_port_copy_frame_rgba(
    uint8_t* destination,
    int32_t capacity,
    int32_t* width,
    int32_t* height,
    uint64_t* generation
);
extern void phoneme_ios_lcdui_reset(void);
extern void phoneme_ios_media_reset(void);
extern int32_t phoneme_ios_lcdui_poll_event(PhoneMELCDUIEvent* event_out);
extern void phoneme_ios_lcdui_select_command(int32_t command_id);
extern void phoneme_ios_lcdui_focus_item(int32_t component_id);
extern void phoneme_ios_lcdui_activate_item(int32_t component_id);
extern void phoneme_ios_lcdui_set_text(
    int32_t component_id,
    const char* utf8_text,
    int32_t caret_position
);
extern void phoneme_ios_lcdui_set_choice(
    int32_t component_id,
    int32_t element_index,
    int32_t selected
);
extern void phoneme_ios_lcdui_set_gauge(
    int32_t component_id,
    int32_t value
);
extern void phoneme_ios_lcdui_set_date(
    int32_t component_id,
    int64_t unix_seconds
);
extern void phoneme_ios_lcdui_set_scroll_position(int32_t position);
extern int32_t phoneme_ios_lcdui_copy_image_rgba(
    int32_t component_id,
    uint8_t* destination,
    int32_t capacity,
    int32_t* width,
    int32_t* height,
    uint64_t* generation
);

typedef struct {
    pthread_mutex_t mutex;
    pthread_t worker;
    int hasWorker;
    int running;
    int lastExitCode;
    char* runtimeHome;
    char* classesZip;
    char* jarPath;
    char* mainClass;
    int32_t keyCodes[7];
} PhoneMERuntime;

/* phoneME CLDC/MIDP keeps process-global VM state, so only one VM may run. */
static pthread_mutex_t activeRuntimeMutex = PTHREAD_MUTEX_INITIALIZER;
static PhoneMERuntime* activeRuntime;

static char* duplicate_string(const char* value) {
    size_t length;
    char* result;

    if (value == NULL) {
        return NULL;
    }

    length = strlen(value);
    result = (char*)malloc(length + 1U);
    if (result != NULL) {
        memcpy(result, value, length + 1U);
    }
    return result;
}

static void release_active_runtime(PhoneMERuntime* runtime) {
    pthread_mutex_lock(&activeRuntimeMutex);
    if (activeRuntime == runtime) {
        activeRuntime = NULL;
    }
    pthread_mutex_unlock(&activeRuntimeMutex);
}

static void reset_host_session_resources(void) {
    /* These bridges are process-global just like the CLDC VM. Reset them only
     * after the worker has fully exited so no stale framebuffer, LCDUI event,
     * player, tone, vibration or stop flag leaks into the next MIDlet. */
    phoneme_ios_media_reset();
    phoneme_ios_lcdui_reset();
    phoneme_ios_port_reset();
}

typedef struct {
    PhoneMERuntime* runtime;
    char* runtimeHome;
    char* classesZip;
    char* jarPath;
    char* mainClass;
    char* combinedClassPath;
    int result;
} PhoneMEWorkerCleanup;

static void finish_midlet_worker(void* context) {
    PhoneMEWorkerCleanup* cleanup = (PhoneMEWorkerCleanup*)context;
    PhoneMERuntime* runtime = cleanup->runtime;

    free(cleanup->runtimeHome);
    free(cleanup->classesZip);
    free(cleanup->jarPath);
    free(cleanup->mainClass);
    free(cleanup->combinedClassPath);
    phoneme_ios_media_reset();

    pthread_mutex_lock(&runtime->mutex);
    runtime->lastExitCode = cleanup->result;
    runtime->running = 0;
    pthread_mutex_unlock(&runtime->mutex);

    release_active_runtime(runtime);
}

static void* run_midlet_worker(void* context) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)context;

#if defined(__APPLE__)
    /* Run promptly without competing with UIKit's main/render threads at the
     * user-interactive tier. Frame conversion is already off the main thread. */
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    (void)pthread_setname_np("phoneME VM");
#endif
    PhoneMEWorkerCleanup cleanup;
    char* runtimeHome;
    char* classesZip;
    char* jarPath;
    char* mainClass;
    char* combinedClassPath = NULL;
    /*
     * Keep enough heap committed to absorb short-lived animation, combat and
     * network bursts without forcing the VM's stop-the-world heap expansion
     * path. The previous 128M maximum still started with only 1M committed.
     */
    char heapCapacityArgument[] = "=HeapCapacity64M";
    char classpathOption[] = "-classpathext";
    char internalSuite[] = "internal";
    char* arguments[7];
    int result;

    memset(&cleanup, 0, sizeof(cleanup));
    cleanup.runtime = runtime;
    cleanup.result = PHONEME_OK;
    pthread_cleanup_push(finish_midlet_worker, &cleanup);

    pthread_mutex_lock(&runtime->mutex);
    runtimeHome = duplicate_string(runtime->runtimeHome);
    classesZip = duplicate_string(runtime->classesZip);
    jarPath = duplicate_string(runtime->jarPath);
    mainClass = duplicate_string(runtime->mainClass);
    pthread_mutex_unlock(&runtime->mutex);
    cleanup.runtimeHome = runtimeHome;
    cleanup.classesZip = classesZip;
    cleanup.jarPath = jarPath;
    cleanup.mainClass = mainClass;

    if (runtimeHome == NULL || classesZip == NULL || jarPath == NULL ||
            mainClass == NULL) {
        result = PHONEME_ERROR_INVALID_ARGUMENT;
        goto finished;
    }

    {
        size_t classesLength = strlen(classesZip);
        size_t jarLength = strlen(jarPath);
        if (classesLength > ((size_t)-1) - jarLength - 2U) {
            result = PHONEME_ERROR_INVALID_ARGUMENT;
            goto finished;
        }
        combinedClassPath = (char*)malloc(classesLength + jarLength + 2U);
        if (combinedClassPath == NULL) {
            result = PHONEME_ERROR_INVALID_ARGUMENT;
            goto finished;
        }
        memcpy(combinedClassPath, classesZip, classesLength);
        combinedClassPath[classesLength] = ':';
        memcpy(
            combinedClassPath + classesLength + 1U,
            jarPath,
            jarLength + 1U
        );
        cleanup.combinedClassPath = combinedClassPath;
    }

    /* getApplicationDir/getConfigurationDir intentionally honor MIDP_HOME. */
    setenv("MIDP_HOME", runtimeHome, 1);
    phoneme_cldc_set_should_stop_callback(phoneme_ios_port_should_stop);

    arguments[0] = (char*)"runMidlet";
    arguments[1] = heapCapacityArgument;
    arguments[2] = classpathOption;
    arguments[3] = combinedClassPath;
    arguments[4] = internalSuite;
    arguments[5] = mainClass;
    /*
     * Pass the JAR as MIDlet arg0 as well as on the class path. The CLDC
     * suite loader uses arg0 to read META-INF/MANIFEST.MF for direct-JAR
     * launches. Without this, MIDlet.getAppProperty() always sees an empty
     * property set, breaking games that select code paths, versions or
     * network servers from manifest attributes.
     */
    arguments[6] = jarPath;

    fprintf(
        stderr,
        "PHONEME_WORKER_START jar=%s main=%s heap=%s\n",
        jarPath,
        mainClass,
        heapCapacityArgument
    );
    fflush(stderr);
    result = runMidlet(7, arguments);
    fprintf(
        stderr,
        "PHONEME_WORKER_EXIT jar=%s main=%s result=%d\n",
        jarPath,
        mainClass,
        result
    );
    fflush(stderr);

finished:
    cleanup.result = result;
    pthread_cleanup_pop(1);
    return NULL;
}

PhoneMERuntimeRef phoneme_create(void) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&runtime->mutex, NULL) != 0) {
        free(runtime);
        return NULL;
    }

    runtime->lastExitCode = PHONEME_OK;
    runtime->keyCodes[0] = -1;
    runtime->keyCodes[1] = -2;
    runtime->keyCodes[2] = -3;
    runtime->keyCodes[3] = -4;
    runtime->keyCodes[4] = -5;
    runtime->keyCodes[5] = -6;
    runtime->keyCodes[6] = -7;
    return (PhoneMERuntimeRef)runtime;
}

void phoneme_destroy(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    pthread_t worker;
    int shouldJoin = 0;

    if (runtime == NULL) {
        return;
    }

    phoneme_stop(runtimeRef);

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->hasWorker) {
        worker = runtime->worker;
        runtime->hasWorker = 0;
        shouldJoin = 1;
    }
    pthread_mutex_unlock(&runtime->mutex);

    if (shouldJoin && !pthread_equal(pthread_self(), worker)) {
        fprintf(stderr, "PHONEME_WORKER_JOIN_BEGIN\n");
        fflush(stderr);
        pthread_join(worker, NULL);
        fprintf(stderr, "PHONEME_WORKER_JOIN_END\n");
        fflush(stderr);
    }

    reset_host_session_resources();
    release_active_runtime(runtime);
    free(runtime->runtimeHome);
    free(runtime->classesZip);
    free(runtime->jarPath);
    free(runtime->mainClass);
    pthread_mutex_destroy(&runtime->mutex);
    free(runtime);
}

int32_t phoneme_configure(
        PhoneMERuntimeRef runtimeRef,
        const char* runtimeHome,
        const char* classesZip) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    char* newHome;
    char* newClasses;

    if (runtime == NULL || runtimeHome == NULL || classesZip == NULL) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    newHome = duplicate_string(runtimeHome);
    newClasses = duplicate_string(classesZip);
    if (newHome == NULL || newClasses == NULL) {
        free(newHome);
        free(newClasses);
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->running) {
        pthread_mutex_unlock(&runtime->mutex);
        free(newHome);
        free(newClasses);
        return PHONEME_ERROR_ALREADY_RUNNING;
    }

    free(runtime->runtimeHome);
    free(runtime->classesZip);
    runtime->runtimeHome = newHome;
    runtime->classesZip = newClasses;
    pthread_mutex_unlock(&runtime->mutex);
    return PHONEME_OK;
}

int32_t phoneme_configure_keymap(
        PhoneMERuntimeRef runtimeRef,
        int32_t up,
        int32_t down,
        int32_t left,
        int32_t right,
        int32_t fire,
        int32_t soft1,
        int32_t soft2) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;

    if (runtime == NULL || up == 0 || down == 0 || left == 0 || right == 0 ||
            fire == 0 || soft1 == 0 || soft2 == 0) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->running) {
        pthread_mutex_unlock(&runtime->mutex);
        return PHONEME_ERROR_ALREADY_RUNNING;
    }
    runtime->keyCodes[0] = up;
    runtime->keyCodes[1] = down;
    runtime->keyCodes[2] = left;
    runtime->keyCodes[3] = right;
    runtime->keyCodes[4] = fire;
    runtime->keyCodes[5] = soft1;
    runtime->keyCodes[6] = soft2;
    pthread_mutex_unlock(&runtime->mutex);
    return PHONEME_OK;
}

int32_t phoneme_start_jar(
        PhoneMERuntimeRef runtimeRef,
        const char* jarPath,
        const char* mainClass,
        int32_t screenWidth,
        int32_t screenHeight) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    pthread_t completedWorker;
    int shouldJoin = 0;
    char* newJarPath;
    char* newMainClass;
    int createResult;

    if (runtime == NULL || jarPath == NULL || mainClass == NULL ||
            mainClass[0] == '\0' || screenWidth < 1 || screenHeight < 1 ||
            screenWidth > 2048 || screenHeight > 2048) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    newJarPath = duplicate_string(jarPath);
    newMainClass = duplicate_string(mainClass);
    if (newJarPath == NULL || newMainClass == NULL) {
        free(newJarPath);
        free(newMainClass);
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->running) {
        pthread_mutex_unlock(&runtime->mutex);
        free(newJarPath);
        free(newMainClass);
        return PHONEME_ERROR_ALREADY_RUNNING;
    }
    if (runtime->runtimeHome == NULL || runtime->classesZip == NULL) {
        pthread_mutex_unlock(&runtime->mutex);
        free(newJarPath);
        free(newMainClass);
        return PHONEME_ERROR_NOT_CONFIGURED;
    }
    if (runtime->hasWorker) {
        completedWorker = runtime->worker;
        runtime->hasWorker = 0;
        shouldJoin = 1;
    }
    pthread_mutex_unlock(&runtime->mutex);

    if (shouldJoin && !pthread_equal(pthread_self(), completedWorker)) {
        pthread_join(completedWorker, NULL);
    }

    pthread_mutex_lock(&activeRuntimeMutex);
    if (activeRuntime != NULL && activeRuntime != runtime) {
        pthread_mutex_unlock(&activeRuntimeMutex);
        free(newJarPath);
        free(newMainClass);
        return PHONEME_ERROR_ALREADY_RUNNING;
    }
    activeRuntime = runtime;
    pthread_mutex_unlock(&activeRuntimeMutex);

    pthread_mutex_lock(&runtime->mutex);
    free(runtime->jarPath);
    free(runtime->mainClass);
    runtime->jarPath = newJarPath;
    runtime->mainClass = newMainClass;
    runtime->lastExitCode = PHONEME_OK;
    runtime->running = 1;
    phoneme_ios_keymap_configure(
        runtime->keyCodes[0],
        runtime->keyCodes[1],
        runtime->keyCodes[2],
        runtime->keyCodes[3],
        runtime->keyCodes[4],
        runtime->keyCodes[5],
        runtime->keyCodes[6]
    );
    phoneme_ios_port_configure_display(screenWidth, screenHeight);
    phoneme_ios_port_reset();
    phoneme_ios_lcdui_reset();
    phoneme_ios_media_reset();
    createResult = pthread_create(
        &runtime->worker,
        NULL,
        run_midlet_worker,
        runtime
    );
    if (createResult == 0) {
        runtime->hasWorker = 1;
    } else {
        runtime->running = 0;
    }
    pthread_mutex_unlock(&runtime->mutex);

    if (createResult != 0) {
        release_active_runtime(runtime);
        return PHONEME_ERROR_THREAD_CREATE;
    }
    return PHONEME_OK;
}

void phoneme_stop(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int running;

    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->mutex);
    running = runtime->running;
    pthread_mutex_unlock(&runtime->mutex);

    if (running) {
        /* Let the VM begin unwinding before potentially slower AVFoundation
         * teardown. This also prevents media cleanup from delaying the stop
         * request that unblocks the next launch. */
        phoneme_ios_port_request_stop();
        phoneme_ios_media_reset();
    }
}

int32_t phoneme_is_running(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int running;

    if (runtime == NULL) {
        return 0;
    }

    pthread_mutex_lock(&runtime->mutex);
    running = runtime->running;
    pthread_mutex_unlock(&runtime->mutex);
    return running ? 1 : 0;
}

int32_t phoneme_last_exit_code(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int result;

    if (runtime == NULL) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&runtime->mutex);
    result = runtime->lastExitCode;
    pthread_mutex_unlock(&runtime->mutex);
    return result;
}

void phoneme_send_key(
        PhoneMERuntimeRef runtimeRef,
        int32_t keyCode,
        int32_t pressed) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_port_send_key(
            phoneme_ios_keymap_convert_key_code(keyCode),
            pressed
        );
    }
}

void phoneme_send_pointer(
        PhoneMERuntimeRef runtimeRef,
        int32_t x,
        int32_t y,
        int32_t action) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_port_send_pointer(x, y, action);
    }
}

int32_t phoneme_copy_frame_rgba(
        PhoneMERuntimeRef runtimeRef,
        uint8_t* destination,
        int32_t capacity,
        int32_t* width,
        int32_t* height,
        uint64_t* generation) {
    (void)runtimeRef;
    return phoneme_ios_port_copy_frame_rgba(
        destination,
        capacity,
        width,
        height,
        generation
    );
}

int32_t phoneme_poll_lcdui_event(
        PhoneMERuntimeRef runtimeRef,
        PhoneMELCDUIEvent* eventOut) {
    if (runtimeRef == NULL || eventOut == NULL) {
        return 0;
    }
    return phoneme_ios_lcdui_poll_event(eventOut);
}

void phoneme_lcdui_select_command(
        PhoneMERuntimeRef runtimeRef,
        int32_t commandId) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_lcdui_select_command(commandId);
    }
}

void phoneme_lcdui_focus_item(
        PhoneMERuntimeRef runtimeRef,
        int32_t componentId) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_lcdui_focus_item(componentId);
    }
}

void phoneme_lcdui_activate_item(
        PhoneMERuntimeRef runtimeRef,
        int32_t componentId) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_lcdui_activate_item(componentId);
    }
}

void phoneme_lcdui_set_text(
        PhoneMERuntimeRef runtimeRef,
        int32_t componentId,
        const char* utf8Text,
        int32_t caretPosition) {
    if (phoneme_is_running(runtimeRef) && utf8Text != NULL) {
        phoneme_ios_lcdui_set_text(componentId, utf8Text, caretPosition);
    }
}

void phoneme_lcdui_set_choice(
        PhoneMERuntimeRef runtimeRef,
        int32_t componentId,
        int32_t elementIndex,
        int32_t selected) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_lcdui_set_choice(componentId, elementIndex, selected);
    }
}

void phoneme_lcdui_set_gauge(
        PhoneMERuntimeRef runtimeRef,
        int32_t componentId,
        int32_t value) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_lcdui_set_gauge(componentId, value);
    }
}

void phoneme_lcdui_set_date(
        PhoneMERuntimeRef runtimeRef,
        int32_t componentId,
        int64_t unixSeconds) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_lcdui_set_date(componentId, unixSeconds);
    }
}

void phoneme_lcdui_set_scroll_position(
        PhoneMERuntimeRef runtimeRef,
        int32_t position) {
    if (phoneme_is_running(runtimeRef)) {
        phoneme_ios_lcdui_set_scroll_position(position);
    }
}

int32_t phoneme_copy_lcdui_image_rgba(
        PhoneMERuntimeRef runtimeRef,
        int32_t componentId,
        uint8_t* destination,
        int32_t capacity,
        int32_t* width,
        int32_t* height,
        uint64_t* generation) {
    if (runtimeRef == NULL) {
        if (width != NULL) *width = 0;
        if (height != NULL) *height = 0;
        if (generation != NULL) *generation = 0;
        return 0;
    }
    return phoneme_ios_lcdui_copy_image_rgba(
        componentId,
        destination,
        capacity,
        width,
        height,
        generation
    );
}

#else

PhoneMERuntimeRef phoneme_create(void) {
    return NULL;
}

void phoneme_destroy(PhoneMERuntimeRef runtime) {
    (void)runtime;
}

int32_t phoneme_configure(
        PhoneMERuntimeRef runtime,
        const char* runtime_home,
        const char* classes_zip) {
    (void)runtime;
    (void)runtime_home;
    (void)classes_zip;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_configure_keymap(
        PhoneMERuntimeRef runtime,
        int32_t up,
        int32_t down,
        int32_t left,
        int32_t right,
        int32_t fire,
        int32_t soft1,
        int32_t soft2) {
    (void)runtime;
    (void)up;
    (void)down;
    (void)left;
    (void)right;
    (void)fire;
    (void)soft1;
    (void)soft2;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_start_jar(
        PhoneMERuntimeRef runtime,
        const char* jar_path,
        const char* main_class,
        int32_t screen_width,
        int32_t screen_height) {
    (void)runtime;
    (void)jar_path;
    (void)main_class;
    (void)screen_width;
    (void)screen_height;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

void phoneme_stop(PhoneMERuntimeRef runtime) {
    (void)runtime;
}

int32_t phoneme_is_running(PhoneMERuntimeRef runtime) {
    (void)runtime;
    return 0;
}

int32_t phoneme_last_exit_code(PhoneMERuntimeRef runtime) {
    (void)runtime;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

void phoneme_send_key(
        PhoneMERuntimeRef runtime,
        int32_t key_code,
        int32_t pressed) {
    (void)runtime;
    (void)key_code;
    (void)pressed;
}

void phoneme_send_pointer(
        PhoneMERuntimeRef runtime,
        int32_t x,
        int32_t y,
        int32_t action) {
    (void)runtime;
    (void)x;
    (void)y;
    (void)action;
}

int32_t phoneme_copy_frame_rgba(
        PhoneMERuntimeRef runtime,
        uint8_t* destination,
        int32_t capacity,
        int32_t* width,
        int32_t* height,
        uint64_t* generation) {
    (void)runtime;
    (void)destination;
    (void)capacity;
    if (width != NULL) *width = 0;
    if (height != NULL) *height = 0;
    if (generation != NULL) *generation = 0;
    return 0;
}

int32_t phoneme_poll_lcdui_event(
        PhoneMERuntimeRef runtime,
        PhoneMELCDUIEvent* event_out) {
    (void)runtime;
    (void)event_out;
    return 0;
}

void phoneme_lcdui_select_command(
        PhoneMERuntimeRef runtime,
        int32_t command_id) {
    (void)runtime;
    (void)command_id;
}

void phoneme_lcdui_focus_item(
        PhoneMERuntimeRef runtime,
        int32_t component_id) {
    (void)runtime;
    (void)component_id;
}

void phoneme_lcdui_activate_item(
        PhoneMERuntimeRef runtime,
        int32_t component_id) {
    (void)runtime;
    (void)component_id;
}

void phoneme_lcdui_set_text(
        PhoneMERuntimeRef runtime,
        int32_t component_id,
        const char* utf8_text,
        int32_t caret_position) {
    (void)runtime;
    (void)component_id;
    (void)utf8_text;
    (void)caret_position;
}

void phoneme_lcdui_set_choice(
        PhoneMERuntimeRef runtime,
        int32_t component_id,
        int32_t element_index,
        int32_t selected) {
    (void)runtime;
    (void)component_id;
    (void)element_index;
    (void)selected;
}

void phoneme_lcdui_set_gauge(
        PhoneMERuntimeRef runtime,
        int32_t component_id,
        int32_t value) {
    (void)runtime;
    (void)component_id;
    (void)value;
}

void phoneme_lcdui_set_date(
        PhoneMERuntimeRef runtime,
        int32_t component_id,
        int64_t unix_seconds) {
    (void)runtime;
    (void)component_id;
    (void)unix_seconds;
}

void phoneme_lcdui_set_scroll_position(
        PhoneMERuntimeRef runtime,
        int32_t position) {
    (void)runtime;
    (void)position;
}

int32_t phoneme_copy_lcdui_image_rgba(
        PhoneMERuntimeRef runtime,
        int32_t component_id,
        uint8_t* destination,
        int32_t capacity,
        int32_t* width,
        int32_t* height,
        uint64_t* generation) {
    (void)runtime;
    (void)component_id;
    (void)destination;
    (void)capacity;
    if (width != NULL) *width = 0;
    if (height != NULL) *height = 0;
    if (generation != NULL) *generation = 0;
    return 0;
}

#endif
