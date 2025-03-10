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
uint8_t g_rgb_brightness = 20;

static int s_led_pattern = LED_PATTERN_IDLE;
static tNeopixelContext* neopixel_ctx = NULL;
static int s_num_leds = 0;
static TaskHandle_t s_led_task_handle = NULL;

// Status LED variables
static uint32_t s_status_color = STATUS_COLOR_OFF;
static uint8_t s_status_mode = STATUS_MODE_OFF;
static bool s_status_blink_state = false;

#define ANIMATION_DELAY_MS 50
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
    s_num_leds = num_leds;
    
    neopixel_ctx = neopixel_Init(num_leds, gpio_pin);
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to initialize NeoPixel");
        return;
    }
    
    xTaskCreate(led_control_task, "led_control", 2048, NULL, configMAX_PRIORITIES - 1, &s_led_task_handle);
}

void led_update_pattern(bool usb_connected, bool ble_connected)
{
    int new_pattern = LED_PATTERN_IDLE;
    
    if (usb_connected && ble_connected) {
        new_pattern = LED_PATTERN_BLE_CONNECTED;
    } else if (usb_connected) {
        new_pattern = LED_PATTERN_BLE_ADVERTISING;
    } else if (ble_connected) {
        new_pattern = LED_PATTERN_BLE_CONNECTED;
    } else {
        new_pattern = LED_PATTERN_IDLE;
    }
    
    s_led_pattern = new_pattern;
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

static void led_control_task(void *arg)
{
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "NeoPixel not initialized");
        vTaskDelete(NULL);
        return;
    }

    int position = 0;
    tNeopixel pixels[s_num_leds];

    while (1) {
        // Clear all LEDs except status LED
        for (int i = 1; i < s_num_leds; i++) {
            pixels[i].index = i;
            pixels[i].rgb = rgb_color(0, 0, 0);
        }
        
        // Update status LED
        update_status_led(pixels);

        switch (s_led_pattern) {
            case LED_PATTERN_IDLE:
                // Slow breathing blue (excluding LED 0)
                {
                    static int brightness = 0;
                    static bool increasing = true;
                    
                    if (increasing) {
                        brightness += 5;
                        if (brightness >= 100) {
                            increasing = false;
                        }
                    } else {
                        brightness -= 5;
                        if (brightness <= 0) {
                            increasing = true;
                        }
                    }
                    
                    for (int i = 1; i < s_num_leds; i++) {
                        pixels[i].rgb = rgb_color(0, 0, brightness);
                    }
                }
                break;

            case LED_PATTERN_USB_CONNECTED:
                // Green running light (excluding LED 0)
                for (int i = 0; i < 3; i++) {
                    int pos = 1 + ((position + i) % (s_num_leds - 1));
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = rgb_color(0, intensity, 0);
                }
                position = (position + 1) % (s_num_leds - 1);
                break;

            case LED_PATTERN_BLE_ADVERTISING:
                // Blue running light (excluding LED 0)
                for (int i = 0; i < 3; i++) {
                    int pos = 1 + ((position + i) % (s_num_leds - 1));
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = rgb_color(0, 0, intensity);
                }
                position = (position + 1) % (s_num_leds - 1);
                break;

            case LED_PATTERN_BLE_CONNECTED:
                // Purple running light (excluding LED 0)
                for (int i = 0; i < 3; i++) {
                    int pos = 1 + ((position + i) % (s_num_leds - 1));
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = rgb_color(intensity, 0, intensity);
                }
                position = (position + 1) % (s_num_leds - 1);
                break;

            default:
                break;
        }

        neopixel_SetPixel(neopixel_ctx, pixels, s_num_leds);
        vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
    }
}
