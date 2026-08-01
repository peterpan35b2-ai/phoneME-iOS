#ifndef PHONEME_IOS_NATIVE_PORT_H
#define PHONEME_IOS_NATIVE_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void phoneme_ios_port_configure_display(int32_t width, int32_t height);
int32_t phoneme_ios_port_configured_width(void);
int32_t phoneme_ios_port_configured_height(void);

void phoneme_ios_port_reset(void);
void phoneme_ios_port_request_stop(void);
int phoneme_ios_port_should_stop(void);
void phoneme_ios_port_drain_wakeup(void);

void phoneme_ios_port_send_key(int32_t key_code, int32_t pressed);
int phoneme_ios_port_has_pending_key(void);
int phoneme_ios_port_read_key(int32_t* key_code, int32_t* pressed);

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
