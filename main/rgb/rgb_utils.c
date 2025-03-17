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
#include <math.h>
#include "esp_wifi.h"
#include "../utils/storage.h"

static const char *TAG = "RGB_UTILS";
#define BASE_FPS 120
static uint16_t s_current_fps = BASE_FPS;
uint8_t g_rgb_brightness = 35; // Default value, will be updated from settings

#define WIFI_BLINK_FAST_MS 250  // Fast blink period (ms)
#define WIFI_BLINK_SLOW_MS 1000 // Slow blink period (ms)

typedef enum {
    WIFI_ANIM_NONE,
    WIFI_ANIM_APSTA_NOT_CONNECTED,
    WIFI_ANIM_APSTA_CONNECTED,
    WIFI_ANIM_STA_NOT_CONNECTED,
    WIFI_ANIM_STA_CONNECTED,
    BATTERY_CHARGE_LEVEL_WARN,
    BATTERY_CHARGE_LEVEL_LOW,
} status_animation_type_t;

// Pattern definitions
static const led_pattern_t led_patterns[] = {
    // IDLE 
    {
        .colors = {NP_RGB(255, 0, 0), 0},
        .type = ANIM_TYPE_BREATHING,
        .trail_length = 1,
        .speed = 25,
        .direction_up = true
    },
    // USB_CONNECTED
    {
        .colors = {NP_RGB(0, 0, 255), 0},
        .type = ANIM_TYPE_RUNNING_LIGHT_BOUNCE,
        .trail_length = 4,
        .speed = 50, 
        .direction_up = true
    },
    // BLE_CONNECTED 
    {
        .colors = {NP_RGB(255, 0, 255), 0},
        .type = ANIM_TYPE_RUNNING_LIGHT,
        .trail_length = 2,
        .speed = 35, 
        .direction_up = false
    },
    // BOTH_CONNECTED
    {
        .colors = {0},
        .type = ANIM_TYPE_BREATHING,
        .trail_length = 1,
        .speed = 1,
        .direction_up = false
    },
    // SLEEPING
    {
        .colors = {0},
        .type = ANIM_TYPE_BREATHING,
        .trail_length = 1,
        .speed = 1,
        .direction_up = true
    }
};

static int s_led_pattern = LED_PATTERN_IDLE;
static tNeopixelContext* neopixel_ctx = NULL;
static int s_num_leds = 0;
static TaskHandle_t s_led_task_handle = NULL;
static uint32_t s_last_pattern_change_time = 0;
static bool s_in_wakeup_debounce = false;
static uint32_t s_wakeup_debounce_start_time = 0;
#define WAKEUP_DEBOUNCE_MS 200

// Animation state
static uint32_t s_animation_start_time = 0;
static bool s_use_secondary_color = false;
static bool s_direction_up = true;

// Transition state
static bool s_in_transition = false;
static uint32_t s_transition_start_time = 0;
#define TRANSITION_DURATION_MS 120
static tNeopixel* s_previous_state = NULL;

// Status LED variables
static uint32_t s_status_color = STATUS_COLOR_OFF;
static uint8_t s_status_mode = STATUS_MODE_OFF;
static bool s_status_blink_state = false;
static bool s_wifi_apsta_mode = false;
static bool s_wifi_connected = false;
static status_animation_type_t s_wifi_animation = WIFI_ANIM_NONE;
static uint32_t s_last_wifi_blink_time = 0;

#define MIN_CYCLE_TIME_MS 200  // Fastest complete animation cycle: 200ms
#define MAX_CYCLE_TIME_MS 2000 // Slowest complete animation cycle: 2000ms
#define STATUS_BLINK_PERIOD_MS 500
static uint32_t s_last_blink_time = 0;

// Function prototypes
static void update_status_led(tNeopixel* pixels);
static void update_wifi_status_led(tNeopixel* pixels);
static inline void apply_pattern(tNeopixel* pixels, const led_pattern_t* pattern);

static inline uint32_t get_cycle_time_ms(uint8_t speed) {
    if (speed == 0) return MAX_CYCLE_TIME_MS;
    return MIN_CYCLE_TIME_MS + ((MAX_CYCLE_TIME_MS - MIN_CYCLE_TIME_MS) * (100 - speed)) / 100;
}

static inline float get_animation_progress(uint32_t current_time, uint32_t cycle_time) {
    uint32_t elapsed = current_time - s_animation_start_time;
    return (float)(elapsed % cycle_time) / cycle_time;
}

static void led_control_task(void *arg);
static void update_fps_from_settings(void);

uint32_t rgb_color(uint8_t r, uint8_t g, uint8_t b)
{
    r = (r * g_rgb_brightness) / 100;
    g = (g * g_rgb_brightness) / 100;
    b = (b * g_rgb_brightness) / 100;
    
    return NP_RGB(r, g, b);
}

static void update_fps_from_settings(void)
{
    bool low_power_mode = false;
    if (storage_get_bool_setting("power.lowPowerMode", &low_power_mode) == ESP_OK) {
        if (low_power_mode) {
            s_current_fps = BASE_FPS / 2; // 2x lower FPS in low power mode
            ESP_LOGI(TAG, "Low power mode enabled, FPS set to %d", s_current_fps);
        } else {
            s_current_fps = BASE_FPS;
            ESP_LOGI(TAG, "Normal power mode, FPS set to %d", s_current_fps);
        }
    } else {
        s_current_fps = BASE_FPS;
        ESP_LOGW(TAG, "Failed to get power mode from settings, using default FPS");
    }
}

void led_control_init(int num_leds, int gpio_pin)
{
    // Get LED brightness from settings
    int brightness;
    if (storage_get_int_setting("led.brightness", &brightness) == ESP_OK) {
        if (brightness >= 0 && brightness <= 100) {
            g_rgb_brightness = brightness;
            ESP_LOGI(TAG, "LED brightness set to %d%%", brightness);
        } else {
            ESP_LOGW(TAG, "Invalid brightness value %d, using default", brightness);
        }
    } else {
        ESP_LOGW(TAG, "Failed to get brightness from settings, using default");
    }
    
    // Update FPS based on power mode
    update_fps_from_settings();
    
    // Clean up previous resources if any
    if (s_previous_state != NULL) {
        free(s_previous_state);
        s_previous_state = NULL;
    }
    
    s_num_leds = num_leds;
    neopixel_ctx = neopixel_Init(num_leds, gpio_pin);
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to initialize NeoPixel");
        return;
    }
    
    // Allocate memory for previous state array
    s_previous_state = (tNeopixel*)malloc(sizeof(tNeopixel) * num_leds);
    if (s_previous_state == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for LED transition state");
        return;
    }
    
    xTaskCreatePinnedToCore(led_control_task, "led_control", 1500, NULL, 7, &s_led_task_handle, 1);
}

void led_control_deinit(void)
{
    // Stop the LED control task if running
    if (s_led_task_handle != NULL) {
        vTaskDelete(s_led_task_handle);
        s_led_task_handle = NULL;
    }
    
    // Free allocated memory
    if (s_previous_state != NULL) {
        free(s_previous_state);
        s_previous_state = NULL;
    }
    
    // Reset state variables
    s_num_leds = 0;
    s_led_pattern = LED_PATTERN_IDLE;
    s_in_transition = false;
}

void led_update_pattern(bool usb_connected, bool ble_connected, bool ble_paused)
{
    int new_pattern = LED_PATTERN_IDLE;
    uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
    
    if (ble_paused) {
        new_pattern = LED_PATTERN_SLEEPING;
        s_in_wakeup_debounce = false; // Reset debounce state when going to sleep
    } else if (usb_connected && ble_connected) {
        new_pattern = LED_PATTERN_BOTH_CONNECTED;
    } else if (usb_connected) {
        new_pattern = LED_PATTERN_USB_CONNECTED;
    } else if (ble_connected) {
        new_pattern = LED_PATTERN_BLE_CONNECTED;
    }
    
    // Check if we're waking up from sleep
    if (s_led_pattern == LED_PATTERN_SLEEPING && new_pattern != LED_PATTERN_SLEEPING && !s_in_wakeup_debounce) {
        // Start debounce period
        s_in_wakeup_debounce = true;
        s_wakeup_debounce_start_time = current_time;
        return; // Don't update pattern yet
    }
    
    // If we're in debounce period, check if enough time has passed
    if (s_in_wakeup_debounce) {
        if ((current_time - s_wakeup_debounce_start_time) < WAKEUP_DEBOUNCE_MS) {
            return; // Still in debounce period, don't update pattern
        } else {
            // Debounce period over
            s_in_wakeup_debounce = false;
        }
    }
    
    if (new_pattern != s_led_pattern) {
        // Save current LED state for transition
        tNeopixel pixels[s_num_leds];
        for (int i = 0; i < s_num_leds; i++) {
            pixels[i].index = i;
            pixels[i].rgb = 0; // Initialize to off
        }
        
        // Apply current pattern to get current state
        update_status_led(pixels);
        if (s_led_pattern >= 0 && s_led_pattern < sizeof(led_patterns)/sizeof(led_patterns[0])) {
            apply_pattern(pixels, &led_patterns[s_led_pattern]);
        }
        
        // Save current state for transition
        memcpy(s_previous_state, pixels, sizeof(tNeopixel) * s_num_leds);
        
        // Start transition
        s_in_transition = true;
        s_transition_start_time = pdTICKS_TO_MS(xTaskGetTickCount());
        
        // Update pattern state
        s_led_pattern = new_pattern;
        s_animation_start_time = s_transition_start_time;
        s_last_pattern_change_time = s_transition_start_time;
        s_use_secondary_color = false;
        s_direction_up = true;
    }
}

void led_update_status(uint32_t color, uint8_t mode)
{
    // Disable WiFi animation when setting a regular status
    s_wifi_animation = WIFI_ANIM_NONE;
    
    s_status_color = color;
    s_status_mode = mode;
    s_status_blink_state = false;  // Reset blink state when mode changes
    s_last_blink_time = 0;  // Force immediate update
}

// New function to update WiFi status LED
void led_update_wifi_status(bool is_apsta_mode, bool is_connected)
{
    s_wifi_apsta_mode = is_apsta_mode;
    s_wifi_connected = is_connected;
    
    // Determine animation type based on mode and connection status
    if (is_apsta_mode) {
        if (is_connected) {
            s_wifi_animation = WIFI_ANIM_APSTA_CONNECTED;
        } else {
            s_wifi_animation = WIFI_ANIM_APSTA_NOT_CONNECTED;
        }
    } else {
        if (is_connected) {
            s_wifi_animation = WIFI_ANIM_STA_CONNECTED;
        } else {
            s_wifi_animation = WIFI_ANIM_STA_NOT_CONNECTED;
        }
    }
    
    // Reset blink state
    s_status_blink_state = false;
    s_last_wifi_blink_time = 0;
}

static void update_wifi_status_led(tNeopixel* pixels)
{
    uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
    
    // If no WiFi animation is set, return
    if (s_wifi_animation == WIFI_ANIM_NONE) {
        return;
    }
    
    // Determine color and blink period based on animation type
    uint32_t color = STATUS_COLOR_OFF;
    uint32_t blink_period = WIFI_BLINK_FAST_MS;
    
    switch (s_wifi_animation) {
        case WIFI_ANIM_APSTA_NOT_CONNECTED:
            color = STATUS_COLOR_BLUE;
            blink_period = WIFI_BLINK_FAST_MS;
            break;
        case WIFI_ANIM_APSTA_CONNECTED:
            color = STATUS_COLOR_BLUE;
            blink_period = WIFI_BLINK_SLOW_MS;
            break;
        case WIFI_ANIM_STA_NOT_CONNECTED:
            color = STATUS_COLOR_GREEN;
            blink_period = WIFI_BLINK_FAST_MS;
            break;
        case WIFI_ANIM_STA_CONNECTED:
            color = STATUS_COLOR_GREEN;
            blink_period = WIFI_BLINK_SLOW_MS;
            break;
        default:
            return;
    }
    
    // Update blink state if needed
    if ((current_time - s_last_wifi_blink_time) >= blink_period) {
        s_status_blink_state = !s_status_blink_state;
        s_last_wifi_blink_time = current_time;
    }
    
    // Set status LED color based on blink state
    pixels[0].index = 0;
    pixels[0].rgb = s_status_blink_state ? color : STATUS_COLOR_OFF;
}

static void update_status_led(tNeopixel* pixels)
{
    // If WiFi animation is active, use that instead of regular status LED
    if (s_wifi_animation != WIFI_ANIM_NONE) {
        update_wifi_status_led(pixels);
        return;
    }
    
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

static inline void apply_pattern(tNeopixel* pixels, const led_pattern_t* pattern)
{
    int column_length = (s_num_leds - 1) / 2;
    uint32_t current_color = s_use_secondary_color ? pattern->colors[1] : pattern->colors[0];
    uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
    uint32_t cycle_time = get_cycle_time_ms(pattern->speed);
    float progress = get_animation_progress(current_time, cycle_time);

    switch (pattern->type) {
        case ANIM_TYPE_RUNNING_LIGHT_BOUNCE: {
            // Calculate position with bounce
            float bounce_progress;
            if (progress < 0.5f) {
                bounce_progress = progress * 2.0f; // Going down
            } else {
                bounce_progress = 2.0f - (progress * 2.0f); // Going up
            }
            
            // Calculate center position (0 to column_length-1)
            float center_pos = bounce_progress * (column_length - 1);
            
            // Apply to both columns with trail, second column inverted
            for (int col = 0; col < 2; col++) {
                int col_offset = 1 + (col * column_length);
                
                for (int i = 0; i < column_length; i++) {
                    // For second column, invert the position calculation
                    float pos = col == 0 ? i - center_pos : 
                        (column_length - 1 - i) - center_pos;
                    float distance = fabsf(pos);
                    if (distance <= pattern->trail_length) {
                        float intensity = 1.0f - (distance / pattern->trail_length);
                        uint8_t r = ((current_color >> 16) & 0xFF);
                        uint8_t g = ((current_color >> 8) & 0xFF);
                        uint8_t b = (current_color & 0xFF);
                        pixels[col_offset + i].rgb = rgb_color(
                            (r * (int)(intensity * 100)) / 100,
                            (g * (int)(intensity * 100)) / 100,
                            (b * (int)(intensity * 100)) / 100
                        );
                    }
                }
            }
            break;
        }
            
        case ANIM_TYPE_BREATHING: {
            float brightness_progress = progress * 2;
            if (brightness_progress > 1.0f) {
                brightness_progress = 2.0f - brightness_progress; // Fade out
            }
            
            uint32_t color = pattern->colors[0];
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;
            uint32_t result_color = rgb_color(
                (r * (int)(brightness_progress * 100)) / 100,
                (g * (int)(brightness_progress * 100)) / 100,
                (b * (int)(brightness_progress * 100)) / 100
            );
            
            for (int i = 1; i < s_num_leds; i++) {
                pixels[i].rgb = result_color;
            }
            break;
        }

        case ANIM_TYPE_RUNNING_LIGHT: {
            // Calculate base position for each column (0 to column_length)
            float base_pos = progress * column_length;
            
            // Apply to both columns, second column inverted
            for (int col = 0; col < 2; col++) {
                int col_offset = 1 + (col * column_length);
                float base_i = pattern->direction_up ? 
                    base_pos : (column_length - base_pos);
                
                for (int i = 0; i < column_length; i++) {
                    float pos;
                    if (col == 0) {
                        pos = i - base_i;
                    } else {
                        // For second column, invert position but maintain wrapping
                        pos = (column_length - 1 - i) - base_i;
                    }
                    
                    // Calculate wrapped distance for continuous effect
                    float distance = fabsf(pos);
                    if (distance > column_length/2) {
                        distance = column_length - distance;
                    }
                    
                    if (distance <= pattern->trail_length) {
                        float intensity = 1.0f - (distance / pattern->trail_length);
                        uint8_t r = ((current_color >> 16) & 0xFF);
                        uint8_t g = ((current_color >> 8) & 0xFF);
                        uint8_t b = (current_color & 0xFF);
                        pixels[col_offset + i].rgb = rgb_color(
                            (r * (int)(intensity * 100)) / 100,
                            (g * (int)(intensity * 100)) / 100,
                            (b * (int)(intensity * 100)) / 100
                        );
                    }
                }
            }
            break;
        }
    }
}

static void blend_colors(tNeopixel* dest, const tNeopixel* src1, const tNeopixel* src2, float blend_factor)
{
    // Extract RGB components
    uint8_t r1 = (src1->rgb >> 16) & 0xFF;
    uint8_t g1 = (src1->rgb >> 8) & 0xFF;
    uint8_t b1 = src1->rgb & 0xFF;
    
    uint8_t r2 = (src2->rgb >> 16) & 0xFF;
    uint8_t g2 = (src2->rgb >> 8) & 0xFF;
    uint8_t b2 = src2->rgb & 0xFF;
    
    // Linear interpolation between colors
    uint8_t r = r1 + (uint8_t)((float)(r2 - r1) * blend_factor);
    uint8_t g = g1 + (uint8_t)((float)(g2 - g1) * blend_factor);
    uint8_t b = b1 + (uint8_t)((float)(b2 - b1) * blend_factor);
    
    dest->rgb = NP_RGB(r, g, b);
}

static void led_control_task(void *arg)
{
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "NeoPixel not initialized");
        vTaskDelete(NULL);
        return;
    }

    tNeopixel pixels[s_num_leds];
    tNeopixel new_state[s_num_leds];
    
    while (1) {
        // Initialize pixels
        for (int i = 0; i < s_num_leds; i++) {
            pixels[i].index = i;
            pixels[i].rgb = 0;
            new_state[i].index = i;
            new_state[i].rgb = 0;
        }
        
        // Calculate new state based on current pattern
        update_status_led(new_state);
        if (s_led_pattern >= 0 && s_led_pattern < sizeof(led_patterns)/sizeof(led_patterns[0])) {
            apply_pattern(new_state, &led_patterns[s_led_pattern]);
        }
        
        // Handle transition if active
        if (s_in_transition) {
            uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
            uint32_t elapsed = current_time - s_transition_start_time;
            
            if (elapsed >= TRANSITION_DURATION_MS) {
                // Transition complete
                s_in_transition = false;
                memcpy(pixels, new_state, sizeof(tNeopixel) * s_num_leds);
            } else {
                // Calculate blend factor (0.0 to 1.0)
                float blend_factor = (float)elapsed / TRANSITION_DURATION_MS;
                
                // Blend between previous and new state
                for (int i = 0; i < s_num_leds; i++) {
                    blend_colors(&pixels[i], &s_previous_state[i], &new_state[i], blend_factor);
                }
            }
        } else {
            // No transition, use new state directly
            memcpy(pixels, new_state, sizeof(tNeopixel) * s_num_leds);
        }

        neopixel_SetPixel(neopixel_ctx, pixels, s_num_leds);
        vTaskDelay(pdMS_TO_TICKS(1000 / s_current_fps));
    }
}
