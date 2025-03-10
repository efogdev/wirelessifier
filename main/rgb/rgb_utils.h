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

// Status LED colors
#define STATUS_COLOR_OFF    0x000000
#define STATUS_COLOR_RED    0xFF0000
#define STATUS_COLOR_GREEN  0x00FF00
#define STATUS_COLOR_BLUE   0x0000FF
#define STATUS_COLOR_PURPLE 0xFF00FF
#define STATUS_COLOR_WHITE  0xFFFFFF

// Status LED modes
#define STATUS_MODE_OFF     0
#define STATUS_MODE_ON      1
#define STATUS_MODE_BLINK   2

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

// Update status LED (LED 0)
void led_update_status(uint32_t color, uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* _RGB_UTILS_H */
