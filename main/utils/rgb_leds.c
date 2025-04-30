#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "rgb_leds.h"
#include "neopixel.h"
#include <math.h>
#include <vmon.h>

#include "esp_wifi.h"
#include "storage.h"

#define BASE_FPS 120
#define WAKEUP_DEBOUNCE_MS 200
#define TRANSITION_DURATION_MS 120
#define MIN_CYCLE_TIME_MS 200  // Fastest complete animation cycle: 200ms
#define MAX_CYCLE_TIME_MS 2000 // Slowest complete animation cycle: 2000ms
#define STATUS_BLINK_PERIOD_MS 500
#define WIFI_BLINK_FAST_MS 350
#define WIFI_BLINK_SLOW_MS 2000

static const char *TAG = "RGB_UTILS";
static uint16_t s_current_fps = BASE_FPS;
static int s_gpio_pin = 0;
uint8_t g_rgb_brightness = 35;

IRAM_ATTR static uint32_t get_cycle_time_ms(const uint8_t speed) {
    if (speed == 0) return MAX_CYCLE_TIME_MS;
    return MIN_CYCLE_TIME_MS + ((MAX_CYCLE_TIME_MS - MIN_CYCLE_TIME_MS) * (100 - speed)) / 100;
}

// Color utility functions
IRAM_ATTR static uint32_t color_with_brightness(const uint32_t color, const uint8_t brightness) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    r = (r * brightness) / 100;
    g = (g * brightness) / 100;
    b = (b * brightness) / 100;
    
    return NP_RGB(r, g, b);
}

IRAM_ATTR static uint32_t blend_colors(const uint32_t color1, const uint32_t color2, const float blend_factor) {
    const uint8_t r1 = (color1 >> 16) & 0xFF;
    const uint8_t g1 = (color1 >> 8) & 0xFF;
    const uint8_t b1 = color1 & 0xFF;

    const uint8_t r2 = (color2 >> 16) & 0xFF;
    const uint8_t g2 = (color2 >> 8) & 0xFF;
    const uint8_t b2 = color2 & 0xFF;

    const uint8_t r = r1 + (uint8_t)((float)(r2 - r1) * blend_factor);
    const uint8_t g = g1 + (uint8_t)((float)(g2 - g1) * blend_factor);
    const uint8_t b = b1 + (uint8_t)((float)(b2 - b1) * blend_factor);
    
    return NP_RGB(r, g, b);
}

IRAM_ATTR static void extract_rgb(const uint32_t color, uint8_t *r, uint8_t *g, uint8_t *b) {
    *r = (color >> 16) & 0xFF;
    *g = (color >> 8) & 0xFF;
    *b = color & 0xFF;
}

typedef enum {
    WIFI_ANIM_NONE,
    WIFI_ANIM_APSTA_NOT_CONNECTED,
    WIFI_ANIM_APSTA_CONNECTED,
    WIFI_ANIM_STA_NOT_CONNECTED,
    WIFI_ANIM_STA_CONNECTED,
    BATTERY_CHARGE_LEVEL_WARN,
    BATTERY_CHARGE_LEVEL_LOW,
} status_animation_type_t;

// Animation state management
typedef struct {
    uint32_t start_time;
    uint32_t cycle_time;
    float progress;
    bool direction_up;
} animation_state_t;

__attribute__((section(".text"))) static void update_animation_state(animation_state_t *state, const led_pattern_t *pattern) {
    const uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
    state->cycle_time = get_cycle_time_ms(pattern->speed);
    const uint32_t elapsed = current_time - state->start_time;
    state->progress = (float)(elapsed % state->cycle_time) / state->cycle_time;
    state->direction_up = pattern->direction_up;
}

// Status LED management
typedef struct {
    uint32_t color;
    uint8_t mode;
    bool blink_state;
    uint32_t last_blink_time;
    status_animation_type_t animation;
} status_led_state_t;

IRAM_ATTR static void update_status_led_state(status_led_state_t *state, tNeopixel* pixel) {
    const uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
    
    if (state->animation != WIFI_ANIM_NONE) {
        const uint32_t blink_period = (state->animation == WIFI_ANIM_APSTA_CONNECTED ||
                               state->animation == WIFI_ANIM_STA_CONNECTED) ? 
                               WIFI_BLINK_SLOW_MS : WIFI_BLINK_FAST_MS;
        
        if ((current_time - state->last_blink_time) >= blink_period) {
            state->blink_state = !state->blink_state;
            state->last_blink_time = current_time;
        }
        
        pixel->rgb = state->blink_state ? color_with_brightness(state->color, g_rgb_brightness) : STATUS_COLOR_OFF;
    } else {
        switch (state->mode) {
            case STATUS_MODE_ON:
                pixel->rgb = color_with_brightness(state->color, g_rgb_brightness);
                break;
            case STATUS_MODE_BLINK:
                if ((current_time - state->last_blink_time) >= STATUS_BLINK_PERIOD_MS) {
                    state->blink_state = !state->blink_state;
                    state->last_blink_time = current_time;
                }
                pixel->rgb = state->blink_state ? color_with_brightness(state->color, g_rgb_brightness) : STATUS_COLOR_OFF;
                break;
            case STATUS_MODE_OFF:
            default:
                pixel->rgb = STATUS_COLOR_OFF;
                break;
        }
    }
}

static const led_pattern_t led_patterns[] __attribute__((section(".rodata"))) = {
    // IDLE 
    {
        .colors = {NP_RGB(64, 0, 0), 0},
        .type = ANIM_TYPE_BREATHING,
        .trail_length = 1,
        .speed = 25,
        .direction_up = true
    },
    // USB_CONNECTED
    {
        .colors = {NP_RGB(0, 0, 255), 0},
        .type = ANIM_TYPE_RUNNING_LIGHT_BOUNCE,
        .trail_length = 2,
        .speed = 50, 
        .direction_up = true
    },
    // BLE_CONNECTED
    {
        .colors = {NP_RGB(255, 0, 255), 0},
        .type = ANIM_TYPE_RUNNING_LIGHT,
        .trail_length = 1,
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
    },
    // CHARGING
    {
        .colors = {NP_RGB(0, 127, 0), 0},
        .type = ANIM_TYPE_RUNNING_LIGHT,
        .trail_length = 2,
        .speed = 15,
        .direction_up = false
    },
    // BAT_WARNING
    {
        .colors = {NP_RGB(127, 127, 0), 0},
        .type = ANIM_TYPE_BREATHING,
        .trail_length = 1,
        .speed = 1,
        .direction_up = false
    },
    // BAT_LOW
    {
        .colors = {NP_RGB(64, 0, 0), 0},
        .type = ANIM_TYPE_BREATHING,
        .trail_length = 1,
        .speed = 25,
        .direction_up = false
    },
};

static int s_led_pattern = LED_PATTERN_IDLE;
static tNeopixelContext* neopixel_ctx = NULL;
static int s_num_leds = 0;
static TaskHandle_t s_led_task_handle = NULL;
static uint32_t s_last_pattern_change_time = 0;
static bool s_in_wakeup_debounce = false;
static uint32_t s_wakeup_debounce_start_time = 0;

// Animation state
static animation_state_t s_animation_state = {0};
static bool s_use_secondary_color = false;

// Transition state
static bool s_in_transition = false;
static uint32_t s_transition_start_time = 0;
static tNeopixel* s_previous_state = NULL;

// Status LED state initialization in flash
static const status_led_state_t s_status_led_state_init __attribute__((section(".rodata"))) = {
    .color = STATUS_COLOR_OFF,
    .mode = STATUS_MODE_OFF,
    .blink_state = false,
    .last_blink_time = 0,
    .animation = WIFI_ANIM_NONE
};

static status_led_state_t s_status_led_state;
static bool s_wifi_apsta_mode = false;
static bool s_wifi_connected = false;
static void update_status_led(tNeopixel* pixels);
static void apply_pattern(tNeopixel* pixels, const led_pattern_t* pattern);
static void led_control_task(void *arg);
static bool is_task_suspended = false;
static bool is_in_flash_mode = false;

// Function to check if the task should be suspended and handle suspension/resumption
IRAM_ATTR static void check_and_update_task_suspension(void)
{
    if (is_in_flash_mode) {
        return;
    }

    static bool should_suspend = false;
    
    if (s_led_pattern >= 0 && s_led_pattern < sizeof(led_patterns)/sizeof(led_patterns[0])) {
        const led_pattern_t* pattern = &led_patterns[s_led_pattern];
        const bool only_zero_color = (pattern->colors[0] == 0);
        const bool status_led_off = (s_status_led_state.mode == STATUS_MODE_OFF) &&
                            (s_status_led_state.animation == WIFI_ANIM_NONE);
        
        should_suspend = only_zero_color && status_led_off;
    }
    
    if (should_suspend && !is_task_suspended && s_led_task_handle != NULL) {
        is_task_suspended = true;
        ESP_LOGD(TAG, "Suspending LED task - no color and status LED off");
        
        if (neopixel_ctx != NULL) {
            tNeopixel pixels[s_num_leds];
            for (int i = 0; i < s_num_leds; i++) {
                pixels[i].index = i;
                pixels[i].rgb = 0;
            }
            neopixel_SetPixel(neopixel_ctx, pixels, s_num_leds);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        neopixel_Deinit(neopixel_ctx);
        vTaskDelay(pdMS_TO_TICKS(50));
        vTaskSuspend(s_led_task_handle);
    } else if (!should_suspend && is_task_suspended && s_led_task_handle != NULL) {
        ESP_LOGD(TAG, "Resuming LED task - conditions changed");

        neopixel_ctx = neopixel_Init(s_num_leds, s_gpio_pin);
        if (neopixel_ctx == NULL) {
            ESP_LOGE(TAG, "Failed to initialize NeoPixel");
            return;
        }

        vTaskResume(s_led_task_handle);
        is_task_suspended = false;
    }
}

uint32_t rgb_color(const uint8_t r, const uint8_t g, const uint8_t b)
{
    return color_with_brightness(NP_RGB(r, g, b), g_rgb_brightness);
}

void led_control_init(const int num_leds, const int gpio_pin)
{
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
    
    if (s_previous_state != NULL) {
        free(s_previous_state);
        s_previous_state = NULL;
    }

    s_gpio_pin = gpio_pin;
    s_num_leds = num_leds;
    memcpy(&s_status_led_state, &s_status_led_state_init, sizeof(status_led_state_t));
    neopixel_ctx = neopixel_Init(num_leds, gpio_pin);
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to initialize NeoPixel");
        return;
    }
    
    s_previous_state = (tNeopixel*)malloc(sizeof(tNeopixel) * num_leds);
    if (s_previous_state == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for LED transition state");
        return;
    }
    
    xTaskCreatePinnedToCore(led_control_task, "led_control", 1960, NULL, 7, &s_led_task_handle, 1);
}

void led_control_deinit(void)
{
    if (s_led_task_handle != NULL) {
        vTaskDelete(s_led_task_handle);
        s_led_task_handle = NULL;
    }
    
    if (s_previous_state != NULL) {
        free(s_previous_state);
        s_previous_state = NULL;
    }

    if (neopixel_ctx != NULL) {
        neopixel_Deinit(neopixel_ctx);
    }

    s_num_leds = 0;
    s_led_pattern = LED_PATTERN_IDLE;
    s_in_transition = false;
}

IRAM_ATTR void led_update_pattern(const bool usb_connected, const bool ble_connected, const bool ble_paused)
{
    if (is_in_flash_mode) {
        return;
    }

    int new_pattern = LED_PATTERN_IDLE;
    const uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
    const battery_state_t battery_state = get_battery_state();

    // low priority
    if (ble_paused) {
        new_pattern = LED_PATTERN_SLEEPING;
        s_in_wakeup_debounce = false;
    } else if (usb_connected && ble_connected) {
        new_pattern = LED_PATTERN_BOTH_CONNECTED;
    } else if (usb_connected) {
        new_pattern = LED_PATTERN_USB_CONNECTED;
    } else if (ble_connected) {
        new_pattern = LED_PATTERN_BLE_CONNECTED;
    }

    // high priority
    if (battery_state == BATTERY_CHARGING) {
        new_pattern = LED_PATTERN_CHARGING;
    } else if (battery_state == BATTERY_WARNING) {
        new_pattern = LED_PATTERN_BAT_WARNING;
    } else if (battery_state == BATTERY_LOW) {
        new_pattern = LED_PATTERN_BAT_LOW;
    }

    if (s_led_pattern == LED_PATTERN_SLEEPING && new_pattern != LED_PATTERN_SLEEPING && !s_in_wakeup_debounce) {
        s_in_wakeup_debounce = true;
        s_wakeup_debounce_start_time = current_time;
        return;
    }
    
    if (s_in_wakeup_debounce) {
        if (current_time - s_wakeup_debounce_start_time < WAKEUP_DEBOUNCE_MS) {
            return;
        }

        s_in_wakeup_debounce = false;
    }
    
    if (new_pattern != s_led_pattern) {
        tNeopixel pixels[s_num_leds];
        for (int i = 0; i < s_num_leds; i++) {
            pixels[i].index = i;
            pixels[i].rgb = 0;
        }
        
        update_status_led(pixels);
        if (s_led_pattern >= 0 && s_led_pattern < sizeof(led_patterns)/sizeof(led_patterns[0])) {
            apply_pattern(pixels, &led_patterns[s_led_pattern]);
        }
        
        memcpy(s_previous_state, pixels, sizeof(tNeopixel) * s_num_leds);
        
        s_in_transition = true;
        s_transition_start_time = current_time;
        
        s_led_pattern = new_pattern;
        s_animation_state.start_time = current_time;
        s_last_pattern_change_time = current_time;
        s_use_secondary_color = false;
    }
    
    check_and_update_task_suspension();
}

void led_update_status(const uint32_t color, const uint8_t mode)
{
    if (is_in_flash_mode) {
        return;
    }

    s_status_led_state.animation = WIFI_ANIM_NONE;
    s_status_led_state.color = color;
    s_status_led_state.mode = mode;
    s_status_led_state.blink_state = false;
    s_status_led_state.last_blink_time = 0;
    
    check_and_update_task_suspension();
}

void led_update_wifi_status(bool is_apsta_mode, bool is_connected)
{
    if (is_in_flash_mode) {
        return;
    }

    s_wifi_apsta_mode = is_apsta_mode;
    s_wifi_connected = is_connected;
    
    if (is_apsta_mode) {
        s_status_led_state.animation = is_connected ? WIFI_ANIM_APSTA_CONNECTED : WIFI_ANIM_APSTA_NOT_CONNECTED;
        s_status_led_state.color = STATUS_COLOR_BLUE;
    } else {
        s_status_led_state.animation = is_connected ? WIFI_ANIM_STA_CONNECTED : WIFI_ANIM_STA_NOT_CONNECTED;
        s_status_led_state.color = STATUS_COLOR_WHITE;
    }
    
    s_status_led_state.blink_state = false;
    s_status_led_state.last_blink_time = 0;
    
    check_and_update_task_suspension();
}

void IRAM_ATTR rgb_enter_flash_mode(void)
{
    if (neopixel_ctx == NULL) {
        return;
    }

    // ToDo
}

__attribute__((section(".text"))) static void update_status_led(tNeopixel* pixels)
{
    if (is_in_flash_mode) {
        return;
    }

    pixels[0].index = 0;
    update_status_led_state(&s_status_led_state, &pixels[0]);
}

IRAM_ATTR static void apply_pattern(tNeopixel* pixels, const led_pattern_t* pattern)
{
    if (is_task_suspended || is_in_flash_mode) {
        return;
    }

    const int column_length = (s_num_leds - 1) / 2;
    const uint32_t current_color = s_use_secondary_color ? pattern->colors[1] : pattern->colors[0];
    
    update_animation_state(&s_animation_state, pattern);
    const float progress = s_animation_state.progress;

    switch (pattern->type) {
        case ANIM_TYPE_RUNNING_LIGHT_BOUNCE: {
            float bounce_progress;
            if (progress < 0.5f) {
                bounce_progress = progress * 2.0f;
            } else {
                bounce_progress = 2.0f - (progress * 2.0f);
            }
            
            const float center_pos = bounce_progress * (column_length - 1);
            
            for (int col = 0; col < 2; col++) {
                const int col_offset = 1 + (col * column_length);
                
                for (int i = 0; i < column_length; i++) {
                    const float pos = col == 0 ? i - center_pos :
                        (column_length - 1 - i) - center_pos;
                    const float distance = fabsf(pos);
                    if (distance <= pattern->trail_length) {
                        const float intensity = 1.0f - (distance / pattern->trail_length);
                        uint8_t r, g, b;
                        extract_rgb(current_color, &r, &g, &b);
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
                brightness_progress = 2.0f - brightness_progress;
            }

            uint8_t r, g, b;
            extract_rgb(pattern->colors[0], &r, &g, &b);
            const uint32_t result_color = rgb_color(
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
            const float base_pos = progress * column_length;
            
            for (int col = 0; col < 2; col++) {
                const int col_offset = 1 + (col * column_length);
                const float base_i = pattern->direction_up ?
                    base_pos : (column_length - base_pos);
                
                for (int i = 0; i < column_length; i++) {
                    float pos;
                    if (col == 0) {
                        pos = i - base_i;
                    } else {
                        pos = (column_length - 1 - i) - base_i;
                    }
                    
                    float distance = fabsf(pos);
                    if (distance > column_length/2) {
                        distance = column_length - distance;
                    }
                    
                    if (distance <= pattern->trail_length) {
                        const float intensity = 1.0f - (distance / pattern->trail_length);
                        uint8_t r, g, b;
                        extract_rgb(current_color, &r, &g, &b);
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

IRAM_ATTR static void blend_pixel_colors(tNeopixel* dest, const tNeopixel* src1, const tNeopixel* src2, const float blend_factor)
{
    dest->rgb = blend_colors(src1->rgb, src2->rgb, blend_factor);
}

__attribute__((section(".text"))) static void led_control_task(void *arg)
{
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "neopixel lib not initialized");
        vTaskDelete(NULL);
        return;
    }

    tNeopixel pixels[s_num_leds];
    tNeopixel new_state[s_num_leds];
    
    while (1) {
        if (is_task_suspended) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (is_in_flash_mode) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        for (int i = 0; i < s_num_leds; i++) {
            pixels[i].index = i;
            pixels[i].rgb = 0;
            new_state[i].index = i;
            new_state[i].rgb = 0;
        }
        
        update_status_led(new_state);
        if (s_led_pattern >= 0 && s_led_pattern < sizeof(led_patterns)/sizeof(led_patterns[0])) {
            apply_pattern(new_state, &led_patterns[s_led_pattern]);
        }
        
        if (s_in_transition) {
            const uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
            const uint32_t elapsed = current_time - s_transition_start_time;
            
            if (elapsed >= TRANSITION_DURATION_MS) {
                s_in_transition = false;
                memcpy(pixels, new_state, sizeof(tNeopixel) * s_num_leds);
            } else {
                const float blend_factor = (float)elapsed / TRANSITION_DURATION_MS;
                
                for (int i = 0; i < s_num_leds; i++) {
                    blend_pixel_colors(&pixels[i], &s_previous_state[i], &new_state[i], blend_factor);
                }
            }
        } else {
            memcpy(pixels, new_state, sizeof(tNeopixel) * s_num_leds);
        }

        neopixel_SetPixel(neopixel_ctx, pixels, s_num_leds);
        vTaskDelay(pdMS_TO_TICKS(1000 / s_current_fps));
    }
}
