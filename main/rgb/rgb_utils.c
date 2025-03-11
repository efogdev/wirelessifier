/* RGB utility functions and LED control
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rgb_utils.h"
#include "neopixel.h"

static const char *TAG = "RGB_UTILS";
uint8_t g_rgb_brightness = 5;

// Pattern definitions
static const led_pattern_t led_patterns[] = {
    // IDLE 
    {
        .colors = {NP_RGB(128, 0, 0), 0},
        .type = ANIM_TYPE_BREATHING,
        .trail_length = 1,
        .speed = 4,
        .direction_up = true
    },
    // USB_CONNECTED
    {
        .colors = {NP_RGB(0, 0, 128), 0},
        .type = ANIM_TYPE_RUNNING_LIGHT_BOUNCE,
        .trail_length = 2,
        .speed = 1,
        .direction_up = true
    },
    // BLE_CONNECTED 
    {
        .colors = {NP_RGB(128, 0, 128), 0},
        .type = ANIM_TYPE_RUNNING_LIGHT,
        .trail_length = 3,
        .speed = 1,
        .direction_up = false
    },
    // BOTH_CONNECTED
    {
        .colors = {NP_RGB(0, 128, 0)},
        .type = ANIM_TYPE_RUNNING_LIGHT_BOUNCE,
        .trail_length = 3,
        .speed = 2,
        .direction_up = true
    }
};

static int s_led_pattern = LED_PATTERN_IDLE;
static tNeopixelContext* neopixel_ctx = NULL;
static int s_num_leds = 0;
static TaskHandle_t s_led_task_handle = NULL;

// Animation state
static int s_position = 0;
static int s_brightness = 0;
static bool s_increasing = true;
static bool s_use_secondary_color = false;
static bool s_direction_up = true;

// Status LED variables
static uint32_t s_status_color = STATUS_COLOR_OFF;
static uint8_t s_status_mode = STATUS_MODE_OFF;
static bool s_status_blink_state = false;

#define ANIMATION_DELAY_MS 20
#define STATUS_BLINK_PERIOD_MS 500
static uint32_t s_last_blink_time = 0;

static void led_control_task(void *arg);

uint32_t rgb_color(uint8_t r, uint8_t g, uint8_t b)
{
    r = (r * g_rgb_brightness) / 100;
    g = (g * g_rgb_brightness) / 100;
    b = (b * g_rgb_brightness) / 100;
    
    return NP_RGB(r, g, b);
}

void led_control_init(int num_leds, int gpio_pin)
{
    // Validate number of LEDs (must be at least 2 for the columns plus 1 for status)
    // and must be odd (even number for columns plus 1 status LED)
    if (num_leds < 3 || (num_leds - 1) % 2 != 0) {
        ESP_LOGE(TAG, "Invalid number of LEDs: %d. Must be odd and >= 3 (1 status + even number for columns)", num_leds);
        return;
    }
    
    s_num_leds = num_leds;
    
    neopixel_ctx = neopixel_Init(num_leds, gpio_pin);
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to initialize NeoPixel");
        return;
    }
    
    xTaskCreate(led_control_task, "led_control", 8192, NULL, configMAX_PRIORITIES - 1, &s_led_task_handle);
}

void led_update_pattern(bool usb_connected, bool ble_connected)
{
    int new_pattern = LED_PATTERN_IDLE;
    
    if (usb_connected && ble_connected) {
        new_pattern = LED_PATTERN_BOTH_CONNECTED;
    } else if (usb_connected) {
        new_pattern = LED_PATTERN_USB_CONNECTED;
    } else if (ble_connected) {
        new_pattern = LED_PATTERN_BLE_CONNECTED;
    }
    
    if (new_pattern != s_led_pattern) {
        s_led_pattern = new_pattern;
        // Reset animation state
        s_position = 0;
        s_brightness = 0;
        s_increasing = true;
        s_use_secondary_color = false;
        s_direction_up = true;
    }
}

void led_update_status(uint32_t color, uint8_t mode)
{
    s_status_color = color;
    s_status_mode = mode;
    s_status_blink_state = false;  // Reset blink state when mode changes
    s_last_blink_time = 0;  // Force immediate update
}

static void update_status_led(tNeopixel* pixels)
{
    uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
    
    // Update blink state if needed
    if (s_status_mode == STATUS_MODE_BLINK && 
        (current_time - s_last_blink_time) >= STATUS_BLINK_PERIOD_MS) {
        s_status_blink_state = !s_status_blink_state;
        s_last_blink_time = current_time;
    }
    
    // Set status LED color based on mode
    pixels[0].index = 0;
    switch (s_status_mode) {
        case STATUS_MODE_ON:
            pixels[0].rgb = s_status_color;
            break;
        case STATUS_MODE_BLINK:
            pixels[0].rgb = s_status_blink_state ? s_status_color : STATUS_COLOR_OFF;
            break;
        case STATUS_MODE_OFF:
        default:
            pixels[0].rgb = STATUS_COLOR_OFF;
            break;
    }
}

static void apply_pattern(tNeopixel* pixels, const led_pattern_t* pattern)
{
    int column_length = (s_num_leds - 1) / 2;
    uint32_t current_color = s_use_secondary_color ? pattern->colors[1] : pattern->colors[0];

    switch (pattern->type) {
        case ANIM_TYPE_RUNNING_LIGHT_BOUNCE:
            // First column - bouncing upward/downward
            for (int j = 0; j < pattern->trail_length; j++) {
                int trail_pos = s_direction_up ? j : -j;
                int pos = 1 + s_position + trail_pos;
                if (pos >= 1 && pos < 1 + column_length) {
                    uint8_t intensity = 255 >> j;
                    uint32_t color = current_color;
                    uint8_t r = ((color >> 16) & 0xFF) * intensity / 255;
                    uint8_t g = ((color >> 8) & 0xFF) * intensity / 255;
                    uint8_t b = (color & 0xFF) * intensity / 255;
                    pixels[pos].rgb = rgb_color(r, g, b);
                }
            }
            
            // Second column - bouncing upward/downward (opposite direction)
            for (int j = 0; j < pattern->trail_length; j++) {
                int trail_pos = !s_direction_up ? j : -j;
                int pos = s_num_leds - 1 - s_position + trail_pos;
                if (pos >= 1 + column_length && pos < s_num_leds) {
                    uint8_t intensity = 255 >> j;
                    uint32_t color = current_color;
                    uint8_t r = ((color >> 16) & 0xFF) * intensity / 255;
                    uint8_t g = ((color >> 8) & 0xFF) * intensity / 255;
                    uint8_t b = (color & 0xFF) * intensity / 255;
                    pixels[pos].rgb = rgb_color(r, g, b);
                }
            }
            
            // Update positions with bouncing
            if (s_direction_up) {
                s_position++;
                if (s_position >= column_length - 1) {
                    s_direction_up = false;
                    s_position = column_length - 1;
                }
            } else {
                s_position--;
                if (s_position <= 0) {
                    s_direction_up = true;
                    s_position = 0;
                }
            }
            break;
            
        case ANIM_TYPE_BREATHING:
            // Update breathing state
            if (s_increasing) {
                s_brightness += pattern->speed;
                if (s_brightness >= 100) {
                    s_increasing = false;
                }
            } else {
                s_brightness -= pattern->speed;
                if (s_brightness <= 0) {
                    s_increasing = true;
                }
            }
            
            // Apply to both columns
            uint32_t color = pattern->colors[0];
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            uint32_t result_color = rgb_color(
                (r * s_brightness) / 100,
                (g * s_brightness) / 100,
                (b * s_brightness) / 100
            );
            for (int i = 1; i < s_num_leds; i++) {
                pixels[i].rgb = result_color;
            }

            ESP_LOGI(TAG, "R = %d", (r * s_brightness) / 100);
            break;

        case ANIM_TYPE_RUNNING_LIGHT:
        case ANIM_TYPE_ALTERNATING:
            // First column
            for (int j = 0; j < pattern->trail_length; j++) {
                int pos;
                if (pattern->direction_up) {
                    pos = 1 + ((s_position + j) % column_length);
                } else {
                    pos = 1 + ((column_length - 1 - s_position - j + column_length) % column_length);
                }
                uint8_t intensity = 255 >> j;
                uint32_t color = current_color;
                uint8_t r = ((color >> 16) & 0xFF) * intensity / 255;
                uint8_t g = ((color >> 8) & 0xFF) * intensity / 255;
                uint8_t b = (color & 0xFF) * intensity / 255;
                pixels[pos].rgb = rgb_color(r, g, b);
            }
            
            // Second column (opposite direction)
            for (int j = 0; j < pattern->trail_length; j++) {
                int pos;
                if (!pattern->direction_up) {
                    pos = 1 + column_length + ((s_position + j) % column_length);
                } else {
                    pos = 1 + column_length + ((column_length - 1 - s_position - j + column_length) % column_length);
                }
                uint8_t intensity = 255 >> j;
                uint32_t color = current_color;
                uint8_t r = ((color >> 16) & 0xFF) * intensity / 255;
                uint8_t g = ((color >> 8) & 0xFF) * intensity / 255;
                uint8_t b = (color & 0xFF) * intensity / 255;
                pixels[pos].rgb = rgb_color(r, g, b);
            }
            
            s_position = (s_position + 1) % column_length;
            if (s_position == 0 && pattern->type == ANIM_TYPE_ALTERNATING) {
                s_use_secondary_color = !s_use_secondary_color;
            }
            break;
    }
}

static void led_control_task(void *arg)
{
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "NeoPixel not initialized");
        vTaskDelete(NULL);
        return;
    }

    tNeopixel pixels[s_num_leds];

    while (1) {
        for (int i = 1; i < s_num_leds; i++) {
            pixels[i].index = i;
            pixels[i].rgb = 0;
        }
        
        update_status_led(pixels);
        
        if (s_led_pattern >= 0 && s_led_pattern < sizeof(led_patterns)/sizeof(led_patterns[0])) {
            apply_pattern(pixels, &led_patterns[s_led_pattern]);
        }

        neopixel_SetPixel(neopixel_ctx, pixels, s_num_leds);
        vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
    }
}
