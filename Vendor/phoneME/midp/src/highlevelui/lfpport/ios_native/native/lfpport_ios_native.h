#ifndef LFPPORT_IOS_NATIVE_H
#define LFPPORT_IOS_NATIVE_H

#include <stdint.h>
#include <kni.h>

#ifdef __cplusplus
extern "C" {
#endif

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

void phoneme_ios_lcdui_reset(void);
int32_t phoneme_ios_lcdui_poll_event(PhoneMELCDUIEvent* event_out);
void phoneme_ios_lcdui_select_command(int32_t command_id);
void phoneme_ios_lcdui_focus_item(int32_t component_id);
void phoneme_ios_lcdui_activate_item(int32_t component_id);
void phoneme_ios_lcdui_set_text(
    int32_t component_id,
    const char* utf8_text,
    int32_t caret_position
);
void phoneme_ios_lcdui_set_choice(
    int32_t component_id,
    int32_t element_index,
    int32_t selected
);
void phoneme_ios_lcdui_set_gauge(int32_t component_id, int32_t value);
void phoneme_ios_lcdui_set_date(int32_t component_id, int64_t unix_seconds);
void phoneme_ios_lcdui_set_scroll_position(int32_t position);

void phoneme_ios_lcdui_capture_image(
    int32_t component_id,
    jobject image_data
);
void phoneme_ios_lcdui_capture_choice_image(
    int32_t component_id,
    int32_t choice_index,
    jobject image_data
);
int32_t phoneme_ios_lcdui_copy_image_rgba(
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
