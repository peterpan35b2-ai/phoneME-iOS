#ifndef _PHONEME_SDL3_PORT_H_
#define _PHONEME_SDL3_PORT_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int keycode;
    int pressed;
    int repeat;
} PhoneMeSdl3KeyEvent;

typedef struct {
    int x;
    int y;
    int buttonMask;
} PhoneMeSdl3PointerEvent;

void phoneme_sdl3_pump_events(void);
int phoneme_sdl3_has_pending_key(void);
int phoneme_sdl3_has_pending_pointer(void);
int phoneme_sdl3_read_key_event(PhoneMeSdl3KeyEvent* event);
int phoneme_sdl3_read_pointer_event(PhoneMeSdl3PointerEvent* event);

#ifdef __cplusplus
}
#endif

#endif /* _PHONEME_SDL3_PORT_H_ */
