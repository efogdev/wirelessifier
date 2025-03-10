/* RGB utility functions and LED control
 */
#ifndef _RGB_UTILS_H
#define _RGB_UTILS_H

#include <stdint.h>
#include <stdbool.h>
#include "neopixel.h"

#ifdef __cplusplus
extern "C" {
#endif

// LED pattern definitions
#define LED_PATTERN_IDLE 0
#define LED_PATTERN_USB_CONNECTED 1
#define LED_PATTERN_BLE_ADVERTISING 2
#define LED_PATTERN_BLE_CONNECTED 3

// Global brightness setting (0-100%)
extern uint8_t g_rgb_brightness;

// Create RGB value with brightness adjustment
uint32_t rgb_color(uint8_t r, uint8_t g, uint8_t b);

// Initialize LED control
void led_control_init(int num_leds, int gpio_pin);

// Update LED pattern based on connection status
void led_update_pattern(bool usb_connected, bool ble_connected);

#ifdef __cplusplus
}
#endif

#endif /* _RGB_UTILS_H */
