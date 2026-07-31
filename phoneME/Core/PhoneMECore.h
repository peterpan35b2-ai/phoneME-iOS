#ifndef PHONEME_CORE_H
#define PHONEME_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* PhoneMERuntimeRef;

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
    PHONEME_ERROR_THREAD_CREATE = -4
};

PhoneMERuntimeRef phoneme_create(void);
void phoneme_destroy(PhoneMERuntimeRef runtime);

int32_t phoneme_configure(
    PhoneMERuntimeRef runtime,
    const char* runtime_home,
    const char* classes_zip
);

int32_t phoneme_configure_keymap(
    PhoneMERuntimeRef runtime,
    int32_t up,
    int32_t down,
    int32_t left,
    int32_t right,
    int32_t fire,
    int32_t soft1,
    int32_t soft2
);

/** Starts the VM worker and returns after the worker thread is created. */
int32_t phoneme_start_jar(
    PhoneMERuntimeRef runtime,
    const char* jar_path,
    const char* main_class,
    int32_t screen_width,
    int32_t screen_height
);
void phoneme_stop(PhoneMERuntimeRef runtime);
int32_t phoneme_is_running(PhoneMERuntimeRef runtime);
int32_t phoneme_last_exit_code(PhoneMERuntimeRef runtime);

void phoneme_send_key(
    PhoneMERuntimeRef runtime,
    int32_t key_code,
    int32_t pressed
);

void phoneme_send_pointer(
    PhoneMERuntimeRef runtime,
    int32_t x,
    int32_t y,
    int32_t action
);

int32_t phoneme_copy_frame_rgba(
    PhoneMERuntimeRef runtime,
    uint8_t* destination,
    int32_t capacity,
    int32_t* width,
    int32_t* height,
    uint64_t* generation
);

int32_t phoneme_poll_lcdui_event(
    PhoneMERuntimeRef runtime,
    PhoneMELCDUIEvent* event_out
);
void phoneme_lcdui_select_command(
    PhoneMERuntimeRef runtime,
    int32_t command_id
);
void phoneme_lcdui_focus_item(
    PhoneMERuntimeRef runtime,
    int32_t component_id
);
void phoneme_lcdui_activate_item(
    PhoneMERuntimeRef runtime,
    int32_t component_id
);
void phoneme_lcdui_set_text(
    PhoneMERuntimeRef runtime,
    int32_t component_id,
    const char* utf8_text,
    int32_t caret_position
);
void phoneme_lcdui_set_choice(
    PhoneMERuntimeRef runtime,
    int32_t component_id,
    int32_t element_index,
    int32_t selected
);
void phoneme_lcdui_set_gauge(
    PhoneMERuntimeRef runtime,
    int32_t component_id,
    int32_t value
);
void phoneme_lcdui_set_date(
    PhoneMERuntimeRef runtime,
    int32_t component_id,
    int64_t unix_seconds
);
void phoneme_lcdui_set_scroll_position(
    PhoneMERuntimeRef runtime,
    int32_t position
);
int32_t phoneme_copy_lcdui_image_rgba(
    PhoneMERuntimeRef runtime,
    int32_t component_id,
    uint8_t* destination,
    int32_t capacity,
    int32_t* width,
    int32_t* height,
    uint64_t* generation
);

#ifdef __cplusplus
}
#endif

#endif
