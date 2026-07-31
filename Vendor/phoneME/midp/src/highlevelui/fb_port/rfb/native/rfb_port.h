#ifndef _PHONEME_RFB_PORT_H_
#define _PHONEME_RFB_PORT_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int keysym;
    int pressed;
} PhoneMeRfbKeyEvent;

typedef struct {
    int x;
    int y;
    int buttonMask;
} PhoneMeRfbPointerEvent;

int phoneme_rfb_read_key_event(PhoneMeRfbKeyEvent* event);
int phoneme_rfb_read_pointer_event(PhoneMeRfbPointerEvent* event);

#ifdef __cplusplus
}
#endif

#endif /* _PHONEME_RFB_PORT_H_ */
