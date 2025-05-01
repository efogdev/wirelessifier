#pragma once

#include <stdint.h>

typedef void (*button_click_callback_t)(uint8_t button_index);
typedef void (*button_long_press_callback_t)(uint8_t button_index);

void buttons_init(void);
void buttons_deinit(void);
void buttons_subscribe_click(button_click_callback_t callback);
void buttons_subscribe_long_press(button_long_press_callback_t callback);
