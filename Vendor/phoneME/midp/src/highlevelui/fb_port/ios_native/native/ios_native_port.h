#ifndef PHONEME_IOS_NATIVE_PORT_H
#define PHONEME_IOS_NATIVE_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void phoneme_ios_port_configure_display(int32_t width, int32_t height);
void phoneme_ios_port_set_isolate_display(
    int32_t isolate_id,
    int32_t width,
    int32_t height
);
int32_t phoneme_ios_port_configured_width(void);
int32_t phoneme_ios_port_configured_height(void);
int32_t phoneme_ios_port_reverse_orientation(void);
int32_t phoneme_ios_port_toggle_reverse_orientation(void);
int32_t phoneme_ios_port_fullscreen_mode(void);
void phoneme_ios_port_set_fullscreen_mode(int32_t mode);
void phoneme_ios_port_reset_current_display_state(void);

void phoneme_ios_port_reset(void);
void phoneme_ios_port_prepare_foreground(int32_t isolate_id);
void phoneme_ios_port_commit_foreground(int32_t isolate_id);
void phoneme_ios_port_release_isolate(int32_t isolate_id);
void phoneme_ios_port_request_stop(void);
int phoneme_ios_port_should_stop(void);
void phoneme_ios_port_drain_wakeup(void);

void phoneme_ios_port_send_key(int32_t key_code, int32_t pressed);
int phoneme_ios_port_has_pending_key(void);
int phoneme_ios_port_read_key(int32_t* key_code, int32_t* pressed);
int32_t phoneme_ios_port_take_pressed_keys(
    int32_t* destination,
    int32_t capacity
);

void phoneme_ios_port_send_pointer(
    int32_t x,
    int32_t y,
    int32_t action
);
int phoneme_ios_port_has_pending_pointer(void);
int phoneme_ios_port_read_pointer(
    int32_t* x,
    int32_t* y,
    int32_t* action
);
int32_t phoneme_ios_port_take_active_pointer(int32_t* x, int32_t* y);

int32_t phoneme_ios_port_frame_width(void);
int32_t phoneme_ios_port_frame_height(void);
int32_t phoneme_ios_port_copy_frame_rgba(
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
