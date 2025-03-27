#ifndef ROTARY_ENC_H
#define ROTARY_ENC_H

#include <stdint.h>

typedef void (*rotary_callback_t)(int8_t direction);
typedef void (*rotary_click_callback_t)(void);
typedef void (*rotary_long_press_callback_t)(void);

void rotary_enc_init(void);
void rotary_enc_subscribe(rotary_callback_t callback);
void rotary_enc_subscribe_click(rotary_click_callback_t callback);
void rotary_enc_subscribe_long_press(rotary_long_press_callback_t callback);
void rotary_enc_deinit(void);

#endif // ROTARY_ENC_H
