#ifndef PHONEME_CORE_H
#define PHONEME_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* PhoneMERuntimeRef;

typedef struct {
    int32_t attempted;
    int32_t succeeded;
    int32_t failed;
    int32_t skipped;
} PhoneMEPreverifyResult;

int32_t phoneme_preverify_jar_classes(
    const char* runtime_classes_path,
    const char* jar_path,
    const char* class_list_path,
    const char* output_directory,
    PhoneMEPreverifyResult* result_out
);

#define PHONEME_LCDUI_TEXT_CAPACITY 768

typedef enum {
    PHONEME_LCDUI_EVENT_NONE = 0,
    PHONEME_LCDUI_EVENT_RESET = 1,
    PHONEME_LCDUI_EVENT_SCREEN_CREATED = 2,
    PHONEME_LCDUI_EVENT_SCREEN_UPDATED = 3,
    PHONEME_LCDUI_EVENT_SCREEN_SHOWN = 4,
    PHONEME_LCDUI_EVENT_SCREEN_HIDDEN = 5,
    PHONEME_LCDUI_EVENT_SCREEN_DELETED = 6,
    PHONEME_LCDUI_EVENT_ITEM_CREATED = 7,
    PHONEME_LCDUI_EVENT_ITEM_UPDATED = 8,
    PHONEME_LCDUI_EVENT_ITEM_SHOWN = 9,
    PHONEME_LCDUI_EVENT_ITEM_HIDDEN = 10,
    PHONEME_LCDUI_EVENT_ITEM_DELETED = 11,
    PHONEME_LCDUI_EVENT_CHOICE_ELEMENT = 12,
    PHONEME_LCDUI_EVENT_CHOICE_DELETED = 13,
    PHONEME_LCDUI_EVENT_COMMANDS_RESET = 14,
    PHONEME_LCDUI_EVENT_COMMAND = 15,
    PHONEME_LCDUI_EVENT_ITEM_FOCUSED = 16
} PhoneMELCDUIEventKind;

typedef struct {
    int32_t kind;
    int32_t component_id;
    int32_t parent_id;
    int32_t component_type;
    int32_t index;
    int32_t arg0;
    int32_t arg1;
    int32_t arg2;
    int32_t arg3;
    int64_t value64;
    uint64_t generation;
    char text[PHONEME_LCDUI_TEXT_CAPACITY];
    char detail[PHONEME_LCDUI_TEXT_CAPACITY];
} PhoneMELCDUIEvent;

enum {
    PHONEME_OK = 0,
    PHONEME_ERROR_INVALID_ARGUMENT = -1,
    PHONEME_ERROR_ALREADY_RUNNING = -2,
    PHONEME_ERROR_NOT_CONFIGURED = -3,
    PHONEME_ERROR_THREAD_CREATE = -4,
    PHONEME_ERROR_NOT_RUNNING = -5,
    PHONEME_ERROR_SYSTEM_START = -6,
    PHONEME_ERROR_INSTALL = -7,
    PHONEME_ERROR_UNSUPPORTED = -8,
    PHONEME_ERROR_IO = -9,
    PHONEME_ERROR_MALFORMED_INPUT = -10
};

typedef enum {
    PHONEME_APP_STATE_NONE = 0,
    PHONEME_APP_STATE_ACTIVE = 1,
    PHONEME_APP_STATE_PAUSED = 2,
    PHONEME_APP_STATE_DESTROYED = 3,
    PHONEME_APP_STATE_ERROR = 4
} PhoneMEAppState;

typedef enum {
    PHONEME_PUSH_FOREGROUND_ONLY = 0,
    PHONEME_PUSH_SYSTEM_MANAGED = 1
} PhoneMEPushBackgroundPolicy;

typedef enum {
    PHONEME_PUSH_REQUEST_CONNECTION = 1,
    PHONEME_PUSH_REQUEST_ALARM = 2
} PhoneMEPushRequestKind;

#define PHONEME_PUSH_TARGET_CAPACITY 2049
#define PHONEME_PUSH_MIDLET_CAPACITY 513

typedef struct {
    uint64_t request_id;
    int32_t kind;
    int64_t created_at_millis;
    char target[PHONEME_PUSH_TARGET_CAPACITY];
    char midlet[PHONEME_PUSH_MIDLET_CAPACITY];
} PhoneMEPushLaunchRequest;

typedef enum {
    PHONEME_PERMISSION_DOMAIN_UNKNOWN = 0,
    PHONEME_PERMISSION_DOMAIN_NETWORK = 1,
    PHONEME_PERMISSION_DOMAIN_FILESYSTEM = 2,
    PHONEME_PERMISSION_DOMAIN_MEDIA = 3
} PhoneMEPermissionDomain;

typedef enum {
    PHONEME_SUITE_UNTRUSTED = 0,
    PHONEME_SUITE_TRUSTED = 1
} PhoneMESuiteTrust;

typedef enum {
    PHONEME_PERMISSION_UNKNOWN = -1,
    PHONEME_PERMISSION_DENIED = 0,
    PHONEME_PERMISSION_ALLOWED = 1
} PhoneMEPermissionDecision;

typedef enum {
    PHONEME_PERMISSION_ONESHOT = 0,
    PHONEME_PERMISSION_SESSION = 1,
    PHONEME_PERMISSION_BLANKET = 2
} PhoneMEPermissionScope;

typedef struct {
    int32_t suite_id;
    int32_t trust;
    int32_t domain;
    int32_t user_initiated;
    const char* permission;
    const char* resource;
} PhoneMEPermissionRequest;

typedef struct {
    int32_t decision;
    int32_t scope;
} PhoneMEPermissionResponse;

/* Invoked synchronously on the requesting runtime thread. Do not re-enter
 * the same runtime from this callback. String pointers are valid only for
 * the duration of the callback. */
typedef PhoneMEPermissionResponse (*PhoneMEPermissionPromptCallback)(
    void* context,
    const PhoneMEPermissionRequest* request);

PhoneMERuntimeRef phoneme_create(void);
void phoneme_destroy(PhoneMERuntimeRef runtime);
/* optional_class_archive may be NULL. The standalone Core provides its
 * boot classes directly in C++. */
int32_t phoneme_configure(PhoneMERuntimeRef runtime,
                          const char* runtime_home,
                          const char* optional_class_archive);
int32_t phoneme_configure_keymap(PhoneMERuntimeRef runtime,
                                 int32_t up,
                                 int32_t down,
                                 int32_t left,
                                 int32_t right,
                                 int32_t fire,
                                 int32_t soft1,
                                 int32_t soft2);
/* Configure before phoneme_start_system. Passing NULL clears the callback. */
int32_t phoneme_configure_permission_prompt(
    PhoneMERuntimeRef runtime,
    PhoneMEPermissionPromptCallback callback,
    void* context);
/* Call after installation and before phoneme_start_system. */
int32_t phoneme_set_suite_trust(PhoneMERuntimeRef runtime,
                                int32_t suite_id,
                                int32_t trust);
int32_t phoneme_install_jar(PhoneMERuntimeRef runtime,
                            const char* jar_path,
                            int32_t* suite_id_out);
int32_t phoneme_last_install_stage(void);
int32_t phoneme_last_suite_store_stage(void);
int32_t phoneme_start_system(PhoneMERuntimeRef runtime);
int32_t phoneme_start_midlet(PhoneMERuntimeRef runtime,
                             int32_t suite_id,
                             const char* main_class,
                             int32_t app_id,
                             int32_t screen_width,
                             int32_t screen_height);
int32_t phoneme_set_foreground(PhoneMERuntimeRef runtime,
                               int32_t app_id,
                               int32_t screen_width,
                               int32_t screen_height);
int32_t phoneme_pause_midlet(PhoneMERuntimeRef runtime, int32_t app_id);
int32_t phoneme_resume_midlet(PhoneMERuntimeRef runtime, int32_t app_id);
int32_t phoneme_destroy_midlet(PhoneMERuntimeRef runtime, int32_t app_id);
int32_t phoneme_push_set_background_policy(PhoneMERuntimeRef runtime,
                                           int32_t suite_id,
                                           int32_t policy);
int32_t phoneme_push_notify_connection_available(
    PhoneMERuntimeRef runtime,
    int32_t suite_id,
    const char* connection,
    int64_t received_at_millis);
/* Returns the number of eligible requests copied. With destination == NULL
 * and capacity == 0, returns the number currently eligible without dequeuing.
 * Requests remain queued until phoneme_push_acknowledge_launch_request. */
int32_t phoneme_push_poll_launch_requests(
    PhoneMERuntimeRef runtime,
    int32_t suite_id,
    int64_t now_millis,
    int32_t background_execution_granted,
    PhoneMEPushLaunchRequest* destination,
    int32_t capacity);
int32_t phoneme_push_acknowledge_launch_request(
    PhoneMERuntimeRef runtime,
    int32_t suite_id,
    uint64_t request_id);
int32_t phoneme_midlet_state(PhoneMERuntimeRef runtime, int32_t app_id);
int32_t phoneme_foreground_app_id(PhoneMERuntimeRef runtime);
int64_t phoneme_midlet_used_memory(PhoneMERuntimeRef runtime,
                                   int32_t app_id,
                                   int32_t timeout_milliseconds);
int32_t phoneme_start_jar(PhoneMERuntimeRef runtime,
                          const char* jar_path,
                          const char* main_class,
                          int32_t screen_width,
                          int32_t screen_height);
void phoneme_stop(PhoneMERuntimeRef runtime);
void phoneme_suspend(PhoneMERuntimeRef runtime);
void phoneme_resume(PhoneMERuntimeRef runtime);
int32_t phoneme_is_running(PhoneMERuntimeRef runtime);
int32_t phoneme_is_suspended(PhoneMERuntimeRef runtime);
int32_t phoneme_last_exit_code(PhoneMERuntimeRef runtime);

void phoneme_ios_media_set_application_metadata(const char* title,
                                                const char* artist,
                                                const char* artwork_path);
int32_t phoneme_ios_media_has_active_playback(void);

void phoneme_send_key(PhoneMERuntimeRef runtime,
                      int32_t key_code,
                      int32_t pressed);
void phoneme_send_pointer(PhoneMERuntimeRef runtime,
                          int32_t x,
                          int32_t y,
                          int32_t action);
int32_t phoneme_copy_frame_rgba(PhoneMERuntimeRef runtime,
                                uint8_t* destination,
                                int32_t capacity,
                                int32_t* width,
                                int32_t* height,
                                uint64_t* generation);
int32_t phoneme_poll_lcdui_event(PhoneMERuntimeRef runtime,
                                 PhoneMELCDUIEvent* event_out);
void phoneme_lcdui_select_command(PhoneMERuntimeRef runtime,
                                  int32_t command_id);
void phoneme_lcdui_focus_item(PhoneMERuntimeRef runtime,
                              int32_t component_id);
void phoneme_lcdui_activate_item(PhoneMERuntimeRef runtime,
                                 int32_t component_id);
void phoneme_lcdui_set_text(PhoneMERuntimeRef runtime,
                            int32_t component_id,
                            const char* utf8_text,
                            int32_t caret_position);
void phoneme_lcdui_set_choice(PhoneMERuntimeRef runtime,
                              int32_t component_id,
                              int32_t element_index,
                              int32_t selected);
void phoneme_lcdui_set_gauge(PhoneMERuntimeRef runtime,
                             int32_t component_id,
                             int32_t value);
void phoneme_lcdui_set_date(PhoneMERuntimeRef runtime,
                            int32_t component_id,
                            int64_t unix_seconds);
void phoneme_lcdui_set_scroll_position(PhoneMERuntimeRef runtime,
                                       int32_t position);
int32_t phoneme_copy_lcdui_image_rgba(PhoneMERuntimeRef runtime,
                                      int32_t component_id,
                                      uint8_t* destination,
                                      int32_t capacity,
                                      int32_t* width,
                                      int32_t* height,
                                      uint64_t* generation);

#ifdef __cplusplus
}
#endif

#endif
