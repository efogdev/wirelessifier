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
uint8_t g_rgb_brightness = 50;

static int s_led_pattern = LED_PATTERN_IDLE;
static tNeopixelContext* neopixel_ctx = NULL;
static int s_num_leds = 0;
static TaskHandle_t s_led_task_handle = NULL;

#define ANIMATION_DELAY_MS 50

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
    
    xTaskCreate(led_control_task, "LED_Control", 2048, NULL, configMAX_PRIORITIES - 1, &s_led_task_handle);
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
        for (int i = 0; i < s_num_leds; i++) {
            pixels[i].index = i;
            pixels[i].rgb = rgb_color(0, 0, 0);
        }

        switch (s_led_pattern) {
            case LED_PATTERN_IDLE:
                // Slow breathing blue
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
                    
                    for (int i = 0; i < s_num_leds; i++) {
                        pixels[i].rgb = rgb_color(0, 0, brightness);
                    }
                }
                break;

            case LED_PATTERN_USB_CONNECTED:
                // Green running light
                for (int i = 0; i < 3; i++) {
                    int pos = (position + i) % s_num_leds;
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = rgb_color(0, intensity, 0);
                }
                position = (position + 1) % s_num_leds;
                break;

            case LED_PATTERN_BLE_ADVERTISING:
                // Blue running light
                for (int i = 0; i < 3; i++) {
                    int pos = (position + i) % s_num_leds;
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = rgb_color(0, 0, intensity);
                }
                position = (position + 1) % s_num_leds;
                break;

            case LED_PATTERN_BLE_CONNECTED:
                // Purple running light
                for (int i = 0; i < 3; i++) {
                    int pos = (position + i) % s_num_leds;
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = rgb_color(intensity, 0, intensity);
                }
                position = (position + 1) % s_num_leds;
                break;

            default:
                break;
        }

        neopixel_SetPixel(neopixel_ctx, pixels, s_num_leds);
        vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
    }
}
