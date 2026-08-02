#include "PhoneMECore.h"

#include <pthread.h>
#if defined(__APPLE__)
#include <pthread/qos.h>
#include <os/log.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <TargetConditionals.h>

#if TARGET_OS_IOS

/* Run MIDlets as independent phoneME isolates inside one embedded VM so the
 * host can switch foreground applications without destroying background apps. */
#define PHONEME_ENABLE_MVM 1

/* Exported by the minimally selected phoneME MIDP/NAMS objects. Keep these
 * declarations local so the app target does not need the entire phoneME
 * include tree in its header search path. */
typedef uint16_t PhoneMEJChar;
typedef int32_t PhoneMEMIDPError;
typedef int32_t PhoneMESuiteId;

typedef struct PhoneMEGenericListener {
    int dataSize;
    int listenerType;
    void* callback;
    struct PhoneMEGenericListener* next;
} PhoneMEGenericListener;

typedef struct {
    PhoneMEGenericListener genericListener;
    int32_t event;
    int32_t appId;
    int32_t reason;
    int32_t state;
    void* suiteData;
    void* runtimeInfo;
    void* exceptionInfo;
} PhoneMENamsEventData;

typedef struct {
    int32_t memoryReserved;
    int32_t memoryTotal;
    int32_t usedMemory;
    int32_t priority;
    PhoneMEJChar* profileName;
    int32_t profileNameLength;
} PhoneMEMidletRuntimeInfo;

typedef void (*PhoneMENamsListener)(const PhoneMENamsEventData* eventData);

enum {
    PHONEME_NAMS_EVENT_STATE_CHANGED = 1,
    PHONEME_NAMS_EVENT_OPERATION_COMPLETED = 2,
    PHONEME_NAMS_OPERATION_GET_RUNTIME_INFO = 40,
    PHONEME_SYSTEM_STATE_ACTIVE = 1,
    PHONEME_SYSTEM_STATE_STOPPED = 3,
    PHONEME_SYSTEM_STATE_ERROR = 4,
    PHONEME_DISPLAY_STATE_FOREGROUND = 5,
    PHONEME_DISPLAY_STATE_BACKGROUND = 6,
    PHONEME_DISPLAY_STATE_FOREGROUND_REQUEST = 7,
    PHONEME_DISPLAY_STATE_BACKGROUND_REQUEST = 8,
    PHONEME_SYSTEM_EVENT_LISTENER = 1,
    PHONEME_MIDLET_EVENT_LISTENER = 2,
    PHONEME_DISPLAY_EVENT_LISTENER = 3,
    PHONEME_NO_FOREGROUND_APP_ID = 0,
    PHONEME_MAX_APPS = 64
};

extern int runMidlet(int argc, char** commandlineArgs);
extern PhoneMEMIDPError midp_system_initialize(void);
extern PhoneMEMIDPError midp_system_start(void);
extern PhoneMEMIDPError midp_system_stop(void);
extern PhoneMEMIDPError midp_add_event_listener(
    PhoneMENamsListener listener,
    int listenerType
);
extern PhoneMEMIDPError midp_midlet_create_start(
    PhoneMESuiteId suiteId,
    const PhoneMEJChar* className,
    int32_t classNameLength,
    int32_t appId,
    const void* runtimeInfo
);
extern PhoneMEMIDPError midp_midlet_pause(int32_t appId);
extern PhoneMEMIDPError midp_midlet_resume(int32_t appId);
extern PhoneMEMIDPError midp_midlet_destroy(int32_t appId, int32_t timeoutMs);
extern PhoneMEMIDPError midp_midlet_set_foreground(int32_t appId);
extern PhoneMEMIDPError midp_midlet_request_runtime_info(int32_t appId);
extern int midpGetAmsIsolateId(void);
extern int phoneme_ios_nams_enqueue_start(
    PhoneMESuiteId suiteId,
    const PhoneMEJChar* className,
    int32_t classNameLength,
    int32_t appId
);
extern int phoneme_ios_nams_enqueue_set_foreground(int32_t appId);
extern int phoneme_ios_nams_enqueue_pause(int32_t appId);
extern int phoneme_ios_nams_enqueue_resume(int32_t appId);
extern int phoneme_ios_nams_enqueue_destroy(int32_t appId, int32_t timeoutMs);
extern int phoneme_ios_nams_enqueue_stop(void);
extern int phoneme_ios_nams_enqueue_release_input(int32_t isolateId);
extern void phoneme_ios_nams_reset_queue(void);
extern int JVM_ParseOneArg(int argc, char** argv);
extern int fileInstaller(int argc, char** argv);
extern int phoneme_file_installer_last_suite_id(void);
extern int phoneme_file_installer_last_stage(void);
extern int phoneme_suite_store_last_stage(void);
extern void midp_suspend(void);
extern void midp_resume(void);

extern void phoneme_ios_port_configure_display(
    int32_t width,
    int32_t height
);
extern void phoneme_ios_port_set_isolate_display(
    int32_t isolate_id,
    int32_t width,
    int32_t height
);
extern void phoneme_ios_port_prepare_foreground(int32_t isolate_id);
extern void phoneme_ios_port_commit_foreground(int32_t isolate_id);
extern void phoneme_ios_port_release_isolate(int32_t isolate_id);
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
extern void phoneme_ios_lcdui_prepare_foreground(int32_t isolate_id);
extern void phoneme_ios_lcdui_commit_foreground(int32_t isolate_id);
extern void phoneme_ios_media_suspend(void);
extern void phoneme_ios_media_resume(void);
extern void phoneme_ios_media_reset(void);
extern void phoneme_ios_https_reset(void);
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
    pthread_cond_t condition;
    pthread_t worker;
    int hasWorker;
    int running;
    int suspended;
    int systemActive;
    int lastExitCode;
    int32_t foregroundAppId;
    int32_t requestedForegroundAppId;
    int32_t appStates[PHONEME_MAX_APPS + 1];
    int32_t appIsolateIds[PHONEME_MAX_APPS + 1];
    int32_t appScreenWidths[PHONEME_MAX_APPS + 1];
    int32_t appScreenHeights[PHONEME_MAX_APPS + 1];
    int32_t appUsedMemory[PHONEME_MAX_APPS + 1];
    uint64_t appRuntimeInfoGeneration[PHONEME_MAX_APPS + 1];
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
    phoneme_ios_https_reset();
    phoneme_ios_lcdui_reset();
    phoneme_ios_port_reset();
}

#if !PHONEME_ENABLE_MVM
/* Stable single-VM direct-JAR worker used by the iOS runtime. */
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
    runtime->suspended = 0;
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
     * This is a compatibility ceiling, not the initial committed heap. The
     * iOS PCSL mmap chunk starts near 1 MB, grows after a full GC when needed,
     * and can return unused pages while preserving a stable heap address.
     */
    char heapCapacityArgument[] = "=HeapCapacity64M";
    /*
     * The legacy Linux port defaults to a 10 ms scheduler tick, which wakes a
     * dedicated timer thread 100 times per second. A 16 ms tick matches the
     * maximum useful 60 Hz display cadence on iOS while preserving responsive
     * Java timers and significantly reducing foreground timer wakeups.
     */
    char tickIntervalArgument[] = "=TickInterval16";
    char classpathOption[] = "-classpathext";
    char internalSuite[] = "internal";
    char* arguments[8];
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
    arguments[2] = tickIntervalArgument;
    arguments[3] = classpathOption;
    arguments[4] = combinedClassPath;
    arguments[5] = internalSuite;
    arguments[6] = mainClass;
    /*
     * Pass the JAR as MIDlet arg0 as well as on the class path. The CLDC
     * suite loader uses arg0 to read META-INF/MANIFEST.MF for direct-JAR
     * launches. Without this, MIDlet.getAppProperty() always sees an empty
     * property set, breaking games that select code paths, versions or
     * network servers from manifest attributes.
     */
    arguments[7] = jarPath;

    result = runMidlet(8, arguments);

finished:
    cleanup.result = result;
    pthread_cleanup_pop(1);
    return NULL;
}
#endif

#if PHONEME_ENABLE_MVM
static PhoneMERuntime* current_active_runtime(void) {
    PhoneMERuntime* runtime;
    pthread_mutex_lock(&activeRuntimeMutex);
    runtime = activeRuntime;
    pthread_mutex_unlock(&activeRuntimeMutex);
    return runtime;
}

#if defined(__APPLE__)
static void update_mvm_worker_qos(PhoneMERuntime* runtime) {
    int appId;
    int liveApplicationCount = 0;
    int hasForegroundApplication;
    qos_class_t qosClass;

    pthread_mutex_lock(&runtime->mutex);
    hasForegroundApplication =
        runtime->foregroundAppId != PHONEME_NO_FOREGROUND_APP_ID;
    for (appId = 1; appId <= PHONEME_MAX_APPS; ++appId) {
        int32_t state = runtime->appStates[appId];
        if (state == PHONEME_APP_STATE_ACTIVE ||
                state == PHONEME_APP_STATE_PAUSED) {
            ++liveApplicationCount;
        }
    }
    pthread_mutex_unlock(&runtime->mutex);

    if (!hasForegroundApplication && liveApplicationCount > 0) {
        qosClass = QOS_CLASS_UTILITY;
    } else if (liveApplicationCount > 1) {
        qosClass = QOS_CLASS_DEFAULT;
    } else {
        qosClass = QOS_CLASS_USER_INITIATED;
    }
    (void)pthread_set_qos_class_self_np(qosClass, 0);
}
#else
static void update_mvm_worker_qos(PhoneMERuntime* runtime) {
    (void)runtime;
}
#endif

static void nams_system_listener(const PhoneMENamsEventData* eventData) {
    PhoneMERuntime* runtime = current_active_runtime();
    int detachHost = 0;
    if (runtime == NULL || eventData == NULL) {
        return;
    }

    if (eventData->event == PHONEME_NAMS_EVENT_OPERATION_COMPLETED &&
            eventData->reason == PHONEME_NAMS_OPERATION_GET_RUNTIME_INFO &&
            eventData->appId >= 1 && eventData->appId <= PHONEME_MAX_APPS) {
        const PhoneMEMidletRuntimeInfo* info =
            (const PhoneMEMidletRuntimeInfo*)eventData->runtimeInfo;
        pthread_mutex_lock(&runtime->mutex);
        runtime->appUsedMemory[eventData->appId] =
            eventData->state == PHONEME_OK && info != NULL
                ? info->usedMemory : -1;
        runtime->appRuntimeInfoGeneration[eventData->appId] += 1U;
        pthread_cond_broadcast(&runtime->condition);
        pthread_mutex_unlock(&runtime->mutex);
        return;
    }

    if (eventData->event != PHONEME_NAMS_EVENT_STATE_CHANGED) {
        return;
    }

    pthread_mutex_lock(&runtime->mutex);
    if (eventData->state == PHONEME_SYSTEM_STATE_ACTIVE) {
        runtime->systemActive = 1;
        runtime->lastExitCode = PHONEME_OK;
    } else if (eventData->state == PHONEME_SYSTEM_STATE_STOPPED ||
            eventData->state == PHONEME_SYSTEM_STATE_ERROR) {
        runtime->systemActive = 0;
        runtime->foregroundAppId = 0;
        runtime->requestedForegroundAppId = 0;
        detachHost = 1;
    }
    pthread_cond_broadcast(&runtime->condition);
    pthread_mutex_unlock(&runtime->mutex);
    if (detachHost) {
        phoneme_ios_port_commit_foreground(0);
        phoneme_ios_lcdui_commit_foreground(0);
    }
}

static void nams_midlet_listener(const PhoneMENamsEventData* eventData) {
    PhoneMERuntime* runtime = current_active_runtime();
    int shouldSetForeground = 0;
    int detachedForeground = 0;
    if (runtime == NULL || eventData == NULL ||
            eventData->event != PHONEME_NAMS_EVENT_STATE_CHANGED ||
            eventData->appId < 1 || eventData->appId > PHONEME_MAX_APPS) {
        return;
    }

    fprintf(
        stderr,
        "[phoneME NAMS midlet] app=%d state=%d reason=%d requested=%d foreground=%d isolate=%d\n",
        eventData->appId,
        eventData->state,
        eventData->reason,
        runtime->requestedForegroundAppId,
        runtime->foregroundAppId,
        runtime->appIsolateIds[eventData->appId]
    );
    fflush(stderr);
#if defined(__APPLE__)
    os_log_error(
        OS_LOG_DEFAULT,
        "phoneME NAMS midlet app=%{public}d state=%{public}d reason=%{public}d requested=%{public}d foreground=%{public}d isolate=%{public}d",
        eventData->appId,
        eventData->state,
        eventData->reason,
        runtime->requestedForegroundAppId,
        runtime->foregroundAppId,
        runtime->appIsolateIds[eventData->appId]
    );
#endif

    pthread_mutex_lock(&runtime->mutex);
    runtime->appStates[eventData->appId] = eventData->state;
    if (eventData->reason > 0 &&
            eventData->state != PHONEME_APP_STATE_ERROR &&
            eventData->state != PHONEME_APP_STATE_DESTROYED) {
        runtime->appIsolateIds[eventData->appId] = eventData->reason;
    }
    if (eventData->state == PHONEME_APP_STATE_ACTIVE &&
            runtime->requestedForegroundAppId == eventData->appId &&
            runtime->foregroundAppId != eventData->appId) {
        /* NAMS creates the MIDlet proxy asynchronously. A foreground command
         * queued immediately after create_start races the proxy registration
         * and is rejected as "Invalid App Id", leaving Canvas black. Defer
         * foregrounding until NAMS confirms that the MIDlet is active. */
        shouldSetForeground = 1;
    }
    if (eventData->state == PHONEME_APP_STATE_DESTROYED ||
            eventData->state == PHONEME_APP_STATE_ERROR) {
        runtime->appUsedMemory[eventData->appId] = -1;
        runtime->appRuntimeInfoGeneration[eventData->appId] += 1U;
        runtime->appIsolateIds[eventData->appId] = 0;
        if (runtime->foregroundAppId == eventData->appId) {
            runtime->foregroundAppId = 0;
            detachedForeground = 1;
        }
        if (runtime->requestedForegroundAppId == eventData->appId) {
            runtime->requestedForegroundAppId = 0;
        }
    }
    pthread_cond_broadcast(&runtime->condition);
    pthread_mutex_unlock(&runtime->mutex);

    if (detachedForeground) {
        phoneme_ios_port_commit_foreground(0);
        phoneme_ios_lcdui_commit_foreground(0);
    }
    /* Do not release native display storage from the NAMS lifecycle callback.
     * The isolate can still be unwinding LCDUI/native graphics teardown after
     * it reports DESTROYED/ERROR. finalizeFrameBuffer() owns the safe release
     * point; full runtime reset handles abnormal exits that never finalize. */

    if (shouldSetForeground) {
        int isolateId;
        int screenWidth;
        int screenHeight;
        pthread_mutex_lock(&runtime->mutex);
        isolateId = runtime->appIsolateIds[eventData->appId];
        screenWidth = runtime->appScreenWidths[eventData->appId];
        screenHeight = runtime->appScreenHeights[eventData->appId];
        pthread_mutex_unlock(&runtime->mutex);
        phoneme_ios_port_set_isolate_display(
            isolateId,
            screenWidth,
            screenHeight
        );
        phoneme_ios_port_prepare_foreground(isolateId);
        phoneme_ios_lcdui_prepare_foreground(isolateId);
        if (phoneme_ios_nams_enqueue_set_foreground(eventData->appId) != 0) {
            phoneme_ios_port_commit_foreground(0);
            phoneme_ios_lcdui_commit_foreground(0);
        }
    }
    update_mvm_worker_qos(runtime);
}

static void nams_display_listener(const PhoneMENamsEventData* eventData) {
    PhoneMERuntime* runtime = current_active_runtime();
    int commitIsolateId = -1;
    if (runtime == NULL || eventData == NULL || eventData->appId < 0) {
        return;
    }

    fprintf(
        stderr,
        "[phoneME NAMS display] app=%d state=%d reason=%d requested=%d foreground=%d\n",
        eventData->appId,
        eventData->state,
        eventData->reason,
        runtime->requestedForegroundAppId,
        runtime->foregroundAppId
    );
    fflush(stderr);
#if defined(__APPLE__)
    os_log_error(
        OS_LOG_DEFAULT,
        "phoneME NAMS display app=%{public}d state=%{public}d reason=%{public}d requested=%{public}d foreground=%{public}d",
        eventData->appId,
        eventData->state,
        eventData->reason,
        runtime->requestedForegroundAppId,
        runtime->foregroundAppId
    );
#endif

    pthread_mutex_lock(&runtime->mutex);
    if (eventData->state == PHONEME_DISPLAY_STATE_FOREGROUND ||
            eventData->state == PHONEME_DISPLAY_STATE_FOREGROUND_REQUEST) {
        runtime->foregroundAppId = eventData->appId;
        if (eventData->appId == PHONEME_NO_FOREGROUND_APP_ID) {
            commitIsolateId = 0;
        } else if (eventData->reason > 0) {
            runtime->appIsolateIds[eventData->appId] = eventData->reason;
            commitIsolateId = eventData->reason;
        } else if (eventData->appId <= PHONEME_MAX_APPS) {
            commitIsolateId = runtime->appIsolateIds[eventData->appId];
        }
    } else if ((eventData->state == PHONEME_DISPLAY_STATE_BACKGROUND ||
            eventData->state == PHONEME_DISPLAY_STATE_BACKGROUND_REQUEST) &&
            runtime->foregroundAppId == eventData->appId) {
        runtime->foregroundAppId = 0;
        commitIsolateId = 0;
    }
    pthread_cond_broadcast(&runtime->condition);
    pthread_mutex_unlock(&runtime->mutex);

    if (commitIsolateId >= 0) {
        phoneme_ios_port_commit_foreground(commitIsolateId);
        phoneme_ios_lcdui_commit_foreground(commitIsolateId);
    }
    update_mvm_worker_qos(runtime);
}

static void finish_nams_worker(void* context) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)context;

    pthread_mutex_lock(&runtime->mutex);
    runtime->running = 0;
    runtime->systemActive = 0;
    runtime->foregroundAppId = 0;
    runtime->requestedForegroundAppId = 0;
    memset(runtime->appStates, 0, sizeof(runtime->appStates));
    memset(runtime->appIsolateIds, 0, sizeof(runtime->appIsolateIds));
    memset(runtime->appScreenWidths, 0, sizeof(runtime->appScreenWidths));
    memset(runtime->appScreenHeights, 0, sizeof(runtime->appScreenHeights));
    memset(runtime->appUsedMemory, 0, sizeof(runtime->appUsedMemory));
    memset(
        runtime->appRuntimeInfoGeneration,
        0,
        sizeof(runtime->appRuntimeInfoGeneration)
    );
    pthread_cond_broadcast(&runtime->condition);
    pthread_mutex_unlock(&runtime->mutex);

    phoneme_ios_nams_reset_queue();
    phoneme_ios_media_reset();
    phoneme_ios_https_reset();
    release_active_runtime(runtime);
}

static void* run_nams_worker(void* context) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)context;
    char* runtimeHome;
    char* classesZip;
    char heapCapacityArgument[] = "=HeapCapacity64M";
    char tickIntervalArgument[] = "=TickInterval16";
    char taskPriorityScaleArgument[] = "=TaskPriorityScale4";
    char* heapArguments[] = { heapCapacityArgument };
    char* tickArguments[] = { tickIntervalArgument };
    char* taskPriorityArguments[] = { taskPriorityScaleArgument };
    char* classpathPropertyArgument = NULL;
    char* classpathPropertyArguments[1];
    int result = PHONEME_ERROR_SYSTEM_START;

#if defined(__APPLE__)
    (void)pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    (void)pthread_setname_np("phoneME MVM");
#endif

    pthread_cleanup_push(finish_nams_worker, runtime);

    pthread_mutex_lock(&runtime->mutex);
    runtimeHome = duplicate_string(runtime->runtimeHome);
    classesZip = duplicate_string(runtime->classesZip);
    pthread_mutex_unlock(&runtime->mutex);

    if (runtimeHome == NULL || classesZip == NULL) {
        free(runtimeHome);
        free(classesZip);
        goto finished;
    }

    /* midp_system_start() invokes JVM_Start() with a NULL classpath. The Linux
     * CLDC port used by iOS resolves that through the CLASSPATH environment
     * variable, so publish the bundled CLDC/MIDP classes before bootstrap.
     * NAMS additionally reads the Java property `classpathext` when it creates
     * each child isolate; without it the child cannot load the MIDP suite
     * loader even though the AMS isolate itself can. */
    setenv("MIDP_HOME", runtimeHome, 1);
    setenv("CLASSPATH", classesZip, 1);
    {
        static const char prefix[] = "-Dclasspathext=";
        size_t classesLength = strlen(classesZip);
        size_t argumentLength = sizeof(prefix) + classesLength;
        classpathPropertyArgument = (char*)malloc(argumentLength);
        if (classpathPropertyArgument == NULL) {
            free(runtimeHome);
            free(classesZip);
            goto finished;
        }
        memcpy(classpathPropertyArgument, prefix, sizeof(prefix) - 1U);
        memcpy(
            classpathPropertyArgument + sizeof(prefix) - 1U,
            classesZip,
            classesLength + 1U
        );
        classpathPropertyArguments[0] = classpathPropertyArgument;
    }
    free(runtimeHome);
    free(classesZip);

    phoneme_ios_nams_reset_queue();
    reset_host_session_resources();
    phoneme_cldc_set_should_stop_callback(phoneme_ios_port_should_stop);

    result = midp_system_initialize();
    if (result != 0) {
        goto finished;
    }

    (void)JVM_ParseOneArg(1, heapArguments);
    (void)JVM_ParseOneArg(1, tickArguments);
    (void)JVM_ParseOneArg(1, taskPriorityArguments);
    (void)JVM_ParseOneArg(1, classpathPropertyArguments);
    free(classpathPropertyArgument);
    classpathPropertyArgument = NULL;

    if (midp_add_event_listener(
            nams_system_listener,
            PHONEME_SYSTEM_EVENT_LISTENER) != 0 ||
        midp_add_event_listener(
            nams_midlet_listener,
            PHONEME_MIDLET_EVENT_LISTENER) != 0 ||
        midp_add_event_listener(
            nams_display_listener,
            PHONEME_DISPLAY_EVENT_LISTENER) != 0) {
        result = PHONEME_ERROR_SYSTEM_START;
        goto finished;
    }

    result = midp_system_start();

finished:
    free(classpathPropertyArgument);
    pthread_mutex_lock(&runtime->mutex);
    runtime->lastExitCode = result;
    pthread_mutex_unlock(&runtime->mutex);
    pthread_cleanup_pop(1);
    return NULL;
}

static int wait_for_system_active(PhoneMERuntime* runtime, int timeoutSeconds) {
    struct timespec deadline;
    int active;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeoutSeconds;

    pthread_mutex_lock(&runtime->mutex);
    while (runtime->running && !runtime->systemActive) {
        if (pthread_cond_timedwait(
                &runtime->condition,
                &runtime->mutex,
                &deadline) != 0) {
            break;
        }
    }
    active = runtime->systemActive;
    pthread_mutex_unlock(&runtime->mutex);
    return active;
}

static PhoneMEJChar* ascii_class_name_to_jchars(
        const char* className,
        int32_t* lengthOut) {
    size_t length;
    size_t index;
    PhoneMEJChar* result;

    if (className == NULL || lengthOut == NULL) {
        return NULL;
    }

    length = strlen(className);
    if (length == 0 || length > INT32_MAX) {
        return NULL;
    }

    result = (PhoneMEJChar*)calloc(length, sizeof(*result));
    if (result == NULL) {
        return NULL;
    }
    for (index = 0; index < length; ++index) {
        unsigned char value = (unsigned char)className[index];
        if (value > 0x7fU) {
            free(result);
            return NULL;
        }
        result[index] = (PhoneMEJChar)value;
    }
    *lengthOut = (int32_t)length;
    return result;
}
#endif

PhoneMERuntimeRef phoneme_create(void) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)calloc(1, sizeof(*runtime));
    if (runtime == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&runtime->mutex, NULL) != 0) {
        free(runtime);
        return NULL;
    }
    if (pthread_cond_init(&runtime->condition, NULL) != 0) {
        pthread_mutex_destroy(&runtime->mutex);
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
        pthread_join(worker, NULL);
    }

    reset_host_session_resources();
    release_active_runtime(runtime);
    free(runtime->runtimeHome);
    free(runtime->classesZip);
    free(runtime->jarPath);
    free(runtime->mainClass);
    pthread_cond_destroy(&runtime->condition);
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
    runtime->keyCodes[0] = up;
    runtime->keyCodes[1] = down;
    runtime->keyCodes[2] = left;
    runtime->keyCodes[3] = right;
    runtime->keyCodes[4] = fire;
    runtime->keyCodes[5] = soft1;
    runtime->keyCodes[6] = soft2;
    pthread_mutex_unlock(&runtime->mutex);

    /* The foreground game can have a different keypad profile from background
     * isolates, so key mapping must be switchable while the MVM is running. */
    phoneme_ios_keymap_configure(up, down, left, right, fire, soft1, soft2);
    return PHONEME_OK;
}

#if !PHONEME_ENABLE_MVM
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
    runtime->suspended = 0;
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
#endif

#if PHONEME_ENABLE_MVM
static int valid_display_size(int32_t width, int32_t height) {
    return width > 0 && height > 0 && width <= 2048 && height <= 2048;
}

int32_t phoneme_install_jar(
        PhoneMERuntimeRef runtimeRef,
        const char* jarPath,
        int32_t* suiteIdOut) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    char* runtimeHome;
    char* arguments[2];
    int result;
    int suiteId;

    if (runtime == NULL || jarPath == NULL || jarPath[0] == '\0' ||
            suiteIdOut == NULL) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->running) {
        pthread_mutex_unlock(&runtime->mutex);
        return PHONEME_ERROR_ALREADY_RUNNING;
    }
    runtimeHome = duplicate_string(runtime->runtimeHome);
    pthread_mutex_unlock(&runtime->mutex);

    if (runtimeHome == NULL) {
        return PHONEME_ERROR_NOT_CONFIGURED;
    }

    setenv("MIDP_HOME", runtimeHome, 1);
    free(runtimeHome);

    arguments[0] = (char*)"phoneME-installer";
    arguments[1] = (char*)jarPath;
    result = fileInstaller(2, arguments);
    suiteId = phoneme_file_installer_last_suite_id();
    if (result != 0) {
        return result;
    }
    if (suiteId <= 0) {
        return PHONEME_ERROR_INSTALL;
    }

    *suiteIdOut = (int32_t)suiteId;
    return PHONEME_OK;
}

int32_t phoneme_last_install_stage(void) {
    return (int32_t)phoneme_file_installer_last_stage();
}

int32_t phoneme_last_suite_store_stage(void) {
    return (int32_t)phoneme_suite_store_last_stage();
}

int32_t phoneme_start_system(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    pthread_t completedWorker;
    int shouldJoin = 0;
    int createResult;

    if (runtime == NULL) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->running) {
        pthread_mutex_unlock(&runtime->mutex);
        return wait_for_system_active(runtime, 10)
            ? PHONEME_OK : PHONEME_ERROR_SYSTEM_START;
    }
    if (runtime->runtimeHome == NULL || runtime->classesZip == NULL) {
        pthread_mutex_unlock(&runtime->mutex);
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
        return PHONEME_ERROR_ALREADY_RUNNING;
    }
    activeRuntime = runtime;
    pthread_mutex_unlock(&activeRuntimeMutex);

    pthread_mutex_lock(&runtime->mutex);
    runtime->lastExitCode = PHONEME_OK;
    runtime->running = 1;
    runtime->suspended = 0;
    runtime->systemActive = 0;
    runtime->foregroundAppId = 0;
    runtime->requestedForegroundAppId = 0;
    memset(runtime->appStates, 0, sizeof(runtime->appStates));
    memset(runtime->appIsolateIds, 0, sizeof(runtime->appIsolateIds));
    memset(runtime->appScreenWidths, 0, sizeof(runtime->appScreenWidths));
    memset(runtime->appScreenHeights, 0, sizeof(runtime->appScreenHeights));
    createResult = pthread_create(
        &runtime->worker,
        NULL,
        run_nams_worker,
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

    return wait_for_system_active(runtime, 10)
        ? PHONEME_OK : PHONEME_ERROR_SYSTEM_START;
}

int32_t phoneme_start_midlet(
        PhoneMERuntimeRef runtimeRef,
        int32_t suiteId,
        const char* mainClass,
        int32_t appId,
        int32_t screenWidth,
        int32_t screenHeight) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    PhoneMEJChar* className;
    int32_t classNameLength = 0;
    int previousForegroundIsolateId = 0;
    int result;

    if (runtime == NULL || suiteId <= 0 || appId < 1 ||
            appId > PHONEME_MAX_APPS || mainClass == NULL ||
            !valid_display_size(screenWidth, screenHeight)) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    result = phoneme_start_system(runtimeRef);
    if (result != PHONEME_OK) {
        return result;
    }

    className = ascii_class_name_to_jchars(mainClass, &classNameLength);
    if (className == NULL) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    phoneme_ios_port_configure_display(screenWidth, screenHeight);

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->foregroundAppId >= 1 &&
            runtime->foregroundAppId <= PHONEME_MAX_APPS) {
        previousForegroundIsolateId =
            runtime->appIsolateIds[runtime->foregroundAppId];
    }
    pthread_mutex_unlock(&runtime->mutex);
    if (previousForegroundIsolateId > 0) {
        (void)phoneme_ios_nams_enqueue_release_input(
            previousForegroundIsolateId
        );
    }
    phoneme_ios_port_prepare_foreground(0);
    phoneme_ios_lcdui_prepare_foreground(0);

    pthread_mutex_lock(&runtime->mutex);
    runtime->appStates[appId] = PHONEME_APP_STATE_NONE;
    runtime->appScreenWidths[appId] = screenWidth;
    runtime->appScreenHeights[appId] = screenHeight;
    runtime->requestedForegroundAppId = appId;
    pthread_mutex_unlock(&runtime->mutex);

    result = phoneme_ios_nams_enqueue_start(
        suiteId,
        className,
        classNameLength,
        appId
    );
    free(className);
    if (result != 0) {
        pthread_mutex_lock(&runtime->mutex);
        runtime->appStates[appId] = PHONEME_APP_STATE_ERROR;
        if (runtime->requestedForegroundAppId == appId) {
            runtime->requestedForegroundAppId = 0;
        }
        pthread_mutex_unlock(&runtime->mutex);
        phoneme_ios_port_commit_foreground(previousForegroundIsolateId);
        phoneme_ios_lcdui_commit_foreground(previousForegroundIsolateId);
        return PHONEME_ERROR_SYSTEM_START;
    }

    return PHONEME_OK;
}

int32_t phoneme_set_foreground(
        PhoneMERuntimeRef runtimeRef,
        int32_t appId,
        int32_t screenWidth,
        int32_t screenHeight) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int running;
    int shouldEnqueue;
    int targetIsolateId = 0;
    int previousForegroundIsolateId = 0;
    int result;

    if (runtime == NULL || appId < 0 || appId > PHONEME_MAX_APPS ||
            (appId != PHONEME_NO_FOREGROUND_APP_ID &&
             !valid_display_size(screenWidth, screenHeight))) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }

    pthread_mutex_lock(&runtime->mutex);
    running = runtime->running && runtime->systemActive;
    if (running) {
        runtime->requestedForegroundAppId = appId;
    }
    if (runtime->foregroundAppId >= 1 &&
            runtime->foregroundAppId <= PHONEME_MAX_APPS) {
        previousForegroundIsolateId =
            runtime->appIsolateIds[runtime->foregroundAppId];
    }
    if (appId >= 1 && appId <= PHONEME_MAX_APPS) {
        targetIsolateId = runtime->appIsolateIds[appId];
        runtime->appScreenWidths[appId] = screenWidth;
        runtime->appScreenHeights[appId] = screenHeight;
    }
    shouldEnqueue = appId == PHONEME_NO_FOREGROUND_APP_ID ||
        (appId >= 1 &&
         (runtime->appStates[appId] == PHONEME_APP_STATE_ACTIVE ||
          runtime->appStates[appId] == PHONEME_APP_STATE_PAUSED));
    pthread_mutex_unlock(&runtime->mutex);
    if (!running) {
        return PHONEME_ERROR_NOT_RUNNING;
    }

    if (previousForegroundIsolateId > 0 &&
            previousForegroundIsolateId != targetIsolateId) {
        (void)phoneme_ios_nams_enqueue_release_input(
            previousForegroundIsolateId
        );
    }

    if (appId != PHONEME_NO_FOREGROUND_APP_ID) {
        phoneme_ios_port_configure_display(screenWidth, screenHeight);
        phoneme_ios_port_set_isolate_display(
            targetIsolateId,
            screenWidth,
            screenHeight
        );
        phoneme_ios_port_prepare_foreground(targetIsolateId);
        phoneme_ios_lcdui_prepare_foreground(targetIsolateId);
    } else {
        phoneme_ios_port_prepare_foreground(0);
        phoneme_ios_lcdui_prepare_foreground(0);
        phoneme_ios_port_commit_foreground(0);
        phoneme_ios_lcdui_commit_foreground(0);
    }
    if (!shouldEnqueue) {
        return PHONEME_OK;
    }
    result = phoneme_ios_nams_enqueue_set_foreground(appId);
    if (result != 0) {
        phoneme_ios_port_commit_foreground(previousForegroundIsolateId);
        phoneme_ios_lcdui_commit_foreground(previousForegroundIsolateId);
        return PHONEME_ERROR_SYSTEM_START;
    }
    return PHONEME_OK;
}

int32_t phoneme_pause_midlet(PhoneMERuntimeRef runtimeRef, int32_t appId) {
    if (runtimeRef == NULL || appId < 1 || appId > PHONEME_MAX_APPS) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }
    return phoneme_ios_nams_enqueue_pause(appId) == 0
        ? PHONEME_OK : PHONEME_ERROR_SYSTEM_START;
}

int32_t phoneme_resume_midlet(PhoneMERuntimeRef runtimeRef, int32_t appId) {
    if (runtimeRef == NULL || appId < 1 || appId > PHONEME_MAX_APPS) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }
    return phoneme_ios_nams_enqueue_resume(appId) == 0
        ? PHONEME_OK : PHONEME_ERROR_SYSTEM_START;
}

int32_t phoneme_destroy_midlet(PhoneMERuntimeRef runtimeRef, int32_t appId) {
    if (runtimeRef == NULL || appId < 1 || appId > PHONEME_MAX_APPS) {
        return PHONEME_ERROR_INVALID_ARGUMENT;
    }
    return phoneme_ios_nams_enqueue_destroy(appId, 5000) == 0
        ? PHONEME_OK : PHONEME_ERROR_SYSTEM_START;
}

int32_t phoneme_midlet_state(PhoneMERuntimeRef runtimeRef, int32_t appId) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int32_t state;
    if (runtime == NULL || appId < 1 || appId > PHONEME_MAX_APPS) {
        return PHONEME_APP_STATE_NONE;
    }
    pthread_mutex_lock(&runtime->mutex);
    state = runtime->appStates[appId];
    pthread_mutex_unlock(&runtime->mutex);
    return state;
}

int32_t phoneme_foreground_app_id(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int32_t appId;
    if (runtime == NULL) {
        return 0;
    }
    pthread_mutex_lock(&runtime->mutex);
    appId = runtime->foregroundAppId;
    pthread_mutex_unlock(&runtime->mutex);
    return appId;
}

int64_t phoneme_midlet_used_memory(
        PhoneMERuntimeRef runtimeRef,
        int32_t appId,
        int32_t timeoutMilliseconds) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    uint64_t previousGeneration;
    int32_t state;
    int32_t usedMemory = -1;
    struct timespec deadline;

    if (runtime == NULL || appId < 1 || appId > PHONEME_MAX_APPS) {
        return -1;
    }

    if (timeoutMilliseconds < 1) {
        timeoutMilliseconds = 1;
    } else if (timeoutMilliseconds > 1000) {
        timeoutMilliseconds = 1000;
    }

    pthread_mutex_lock(&runtime->mutex);
    state = runtime->appStates[appId];
    if (!runtime->running || !runtime->systemActive ||
            (state != PHONEME_APP_STATE_ACTIVE &&
             state != PHONEME_APP_STATE_PAUSED)) {
        pthread_mutex_unlock(&runtime->mutex);
        return -1;
    }
    previousGeneration = runtime->appRuntimeInfoGeneration[appId];
    pthread_mutex_unlock(&runtime->mutex);

    if (midp_midlet_request_runtime_info(appId) != PHONEME_OK) {
        return -1;
    }

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeoutMilliseconds / 1000;
    deadline.tv_nsec +=
        (long)(timeoutMilliseconds % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&runtime->mutex);
    while (runtime->running && runtime->systemActive &&
            runtime->appRuntimeInfoGeneration[appId] == previousGeneration) {
        if (pthread_cond_timedwait(
                &runtime->condition,
                &runtime->mutex,
                &deadline) != 0) {
            break;
        }
    }
    if (runtime->appRuntimeInfoGeneration[appId] != previousGeneration &&
            runtime->appUsedMemory[appId] >= 0) {
        usedMemory = runtime->appUsedMemory[appId];
    }
    pthread_mutex_unlock(&runtime->mutex);
    return (int64_t)usedMemory;
}

int32_t phoneme_start_jar(
        PhoneMERuntimeRef runtimeRef,
        const char* jarPath,
        const char* mainClass,
        int32_t screenWidth,
        int32_t screenHeight) {
    int32_t suiteId = 0;
    int result = phoneme_install_jar(runtimeRef, jarPath, &suiteId);
    if (result != PHONEME_OK) {
        return result;
    }
    return phoneme_start_midlet(
        runtimeRef,
        suiteId,
        mainClass,
        1,
        screenWidth,
        screenHeight
    );
}
#else
int32_t phoneme_install_jar(
        PhoneMERuntimeRef runtimeRef,
        const char* jarPath,
        int32_t* suiteIdOut) {
    (void)runtimeRef;
    (void)jarPath;
    if (suiteIdOut != NULL) {
        *suiteIdOut = 0;
    }
    return PHONEME_ERROR_SYSTEM_START;
}

int32_t phoneme_last_install_stage(void) { return 0; }
int32_t phoneme_last_suite_store_stage(void) { return 0; }
int32_t phoneme_start_system(PhoneMERuntimeRef runtimeRef) {
    (void)runtimeRef;
    return PHONEME_ERROR_SYSTEM_START;
}
int32_t phoneme_start_midlet(
        PhoneMERuntimeRef runtimeRef,
        int32_t suiteId,
        const char* mainClass,
        int32_t appId,
        int32_t screenWidth,
        int32_t screenHeight) {
    (void)runtimeRef;
    (void)suiteId;
    (void)mainClass;
    (void)appId;
    (void)screenWidth;
    (void)screenHeight;
    return PHONEME_ERROR_SYSTEM_START;
}
int32_t phoneme_set_foreground(
        PhoneMERuntimeRef runtimeRef,
        int32_t appId,
        int32_t screenWidth,
        int32_t screenHeight) {
    (void)runtimeRef;
    (void)appId;
    (void)screenWidth;
    (void)screenHeight;
    return PHONEME_ERROR_SYSTEM_START;
}
int32_t phoneme_pause_midlet(PhoneMERuntimeRef runtimeRef, int32_t appId) {
    (void)runtimeRef;
    (void)appId;
    return PHONEME_ERROR_SYSTEM_START;
}
int32_t phoneme_resume_midlet(PhoneMERuntimeRef runtimeRef, int32_t appId) {
    (void)runtimeRef;
    (void)appId;
    return PHONEME_ERROR_SYSTEM_START;
}
int32_t phoneme_destroy_midlet(PhoneMERuntimeRef runtimeRef, int32_t appId) {
    (void)runtimeRef;
    (void)appId;
    return PHONEME_ERROR_SYSTEM_START;
}
int32_t phoneme_midlet_state(PhoneMERuntimeRef runtimeRef, int32_t appId) {
    (void)runtimeRef;
    (void)appId;
    return PHONEME_APP_STATE_NONE;
}
int32_t phoneme_foreground_app_id(PhoneMERuntimeRef runtimeRef) {
    (void)runtimeRef;
    return 0;
}
int64_t phoneme_midlet_used_memory(
        PhoneMERuntimeRef runtimeRef,
        int32_t appId,
        int32_t timeoutMilliseconds) {
    (void)runtimeRef;
    (void)appId;
    (void)timeoutMilliseconds;
    return -1;
}
#endif

void phoneme_stop(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int running;
    int suspended;

    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->mutex);
    running = runtime->running;
    suspended = runtime->suspended;
    runtime->suspended = 0;
    pthread_mutex_unlock(&runtime->mutex);

    if (running) {
#if PHONEME_ENABLE_MVM
        int queuedStop;
        int stillRunning;
        struct timespec deadline;

        /* A globally suspended MVM must resume before the VM-thread command
         * queue can dispatch the AMS shutdown request. */
        if (suspended) {
            midp_resume();
        }
        queuedStop = phoneme_ios_nams_enqueue_stop();
        if (queuedStop == 0) {
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += 5;
            pthread_mutex_lock(&runtime->mutex);
            while (runtime->running) {
                if (pthread_cond_timedwait(
                        &runtime->condition,
                        &runtime->mutex,
                        &deadline) != 0) {
                    break;
                }
            }
            stillRunning = runtime->running;
            pthread_mutex_unlock(&runtime->mutex);
        } else {
            stillRunning = 1;
        }
        if (stillRunning) {
            phoneme_ios_port_request_stop();
        }
#else
        if (suspended) {
            midp_resume();
        }
        phoneme_ios_port_request_stop();
#endif
        phoneme_ios_media_reset();
    }
}

void phoneme_suspend(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int shouldSuspend = 0;

    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->running && !runtime->suspended) {
        runtime->suspended = 1;
        shouldSuspend = 1;
    }
    pthread_mutex_unlock(&runtime->mutex);

    if (shouldSuspend) {
        phoneme_ios_media_suspend();
        midp_suspend();
    }
}

void phoneme_resume(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int shouldResume = 0;

    if (runtime == NULL) {
        return;
    }

    pthread_mutex_lock(&runtime->mutex);
    if (runtime->running && runtime->suspended) {
        runtime->suspended = 0;
        shouldResume = 1;
    }
    pthread_mutex_unlock(&runtime->mutex);

    if (shouldResume) {
        midp_resume();
        phoneme_ios_media_resume();
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

int32_t phoneme_is_suspended(PhoneMERuntimeRef runtimeRef) {
    PhoneMERuntime* runtime = (PhoneMERuntime*)runtimeRef;
    int suspended;

    if (runtime == NULL) {
        return 0;
    }

    pthread_mutex_lock(&runtime->mutex);
    suspended = runtime->suspended;
    pthread_mutex_unlock(&runtime->mutex);
    return suspended ? 1 : 0;
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

int32_t phoneme_install_jar(
        PhoneMERuntimeRef runtime,
        const char* jar_path,
        int32_t* suite_id_out) {
    (void)runtime;
    (void)jar_path;
    if (suite_id_out != NULL) *suite_id_out = 0;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_last_install_stage(void) {
    return 0;
}

int32_t phoneme_last_suite_store_stage(void) {
    return 0;
}

int32_t phoneme_start_system(PhoneMERuntimeRef runtime) {
    (void)runtime;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_start_midlet(
        PhoneMERuntimeRef runtime,
        int32_t suite_id,
        const char* main_class,
        int32_t app_id,
        int32_t screen_width,
        int32_t screen_height) {
    (void)runtime;
    (void)suite_id;
    (void)main_class;
    (void)app_id;
    (void)screen_width;
    (void)screen_height;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_set_foreground(
        PhoneMERuntimeRef runtime,
        int32_t app_id,
        int32_t screen_width,
        int32_t screen_height) {
    (void)runtime;
    (void)app_id;
    (void)screen_width;
    (void)screen_height;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_pause_midlet(PhoneMERuntimeRef runtime, int32_t app_id) {
    (void)runtime;
    (void)app_id;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_resume_midlet(PhoneMERuntimeRef runtime, int32_t app_id) {
    (void)runtime;
    (void)app_id;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_destroy_midlet(PhoneMERuntimeRef runtime, int32_t app_id) {
    (void)runtime;
    (void)app_id;
    return PHONEME_ERROR_NOT_CONFIGURED;
}

int32_t phoneme_midlet_state(PhoneMERuntimeRef runtime, int32_t app_id) {
    (void)runtime;
    (void)app_id;
    return PHONEME_APP_STATE_NONE;
}

int32_t phoneme_foreground_app_id(PhoneMERuntimeRef runtime) {
    (void)runtime;
    return 0;
}

int64_t phoneme_midlet_used_memory(
        PhoneMERuntimeRef runtime,
        int32_t app_id,
        int32_t timeout_milliseconds) {
    (void)runtime;
    (void)app_id;
    (void)timeout_milliseconds;
    return -1;
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

void phoneme_suspend(PhoneMERuntimeRef runtime) {
    (void)runtime;
}

void phoneme_resume(PhoneMERuntimeRef runtime) {
    (void)runtime;
}

int32_t phoneme_is_running(PhoneMERuntimeRef runtime) {
    (void)runtime;
    return 0;
}

int32_t phoneme_is_suspended(PhoneMERuntimeRef runtime) {
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
