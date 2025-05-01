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

// Animation types
typedef enum {
    ANIM_TYPE_BREATHING,
    ANIM_TYPE_RUNNING_LIGHT,
    ANIM_TYPE_RUNNING_LIGHT_BOUNCE
} led_animation_type_t;

// LED pattern structure
typedef struct __attribute__((packed)) {
    uint32_t colors[2];           // Primary and secondary colors (for alternating patterns)
    led_animation_type_t type;    // Animation type
    uint8_t trail_length;         // Length of the trail for running light (1-255)
    uint8_t speed;                // Animation speed (1-100, where 100 is max speed)
    bool direction_up;            // Direction for running light animations (true = up, false = down)
} led_pattern_t;

// LED pattern definitions
#define LED_PATTERN_IDLE 0
#define LED_PATTERN_USB_CONNECTED 1
#define LED_PATTERN_BLE_CONNECTED 2
#define LED_PATTERN_BOTH_CONNECTED 3
#define LED_PATTERN_SLEEPING 4
#define LED_PATTERN_CHARGING 5

// Global brightness setting (0-100%)
extern uint8_t g_rgb_brightness;

// Create RGB value with brightness adjustment
uint32_t rgb_color(uint8_t r, uint8_t g, uint8_t b);

// Initialize LED control
void led_control_init(int num_leds, int gpio_pin);

// Deinitialize LED control and free resources
void led_control_deinit(void);

// Update LED pattern based on connection status
void led_update_pattern(bool usb_connected, bool ble_connected, bool ble_paused);

// Update status LED (LED 0)
void led_update_status(uint32_t color, uint8_t mode);

// Update WiFi status LED
void led_update_wifi_status(bool is_apsta_mode, bool is_connected);

// Enter flash mode - stops animations, clears LEDs, sets to dim white
void rgb_enter_flash_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* _RGB_UTILS_H */
