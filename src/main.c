#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "neopixel.h"
#include "usb_hid_host.h"
#include "ble_hid_device.h"
#include "hid_bridge.h"

static const char *TAG = "MAIN";

// LED indicator pins
#define GPIO_WS2812B_PIN GPIO_NUM_38
#define NUM_LEDS 17
#define ANIMATION_DELAY_MS 50

// LED patterns
#define LED_PATTERN_IDLE 0
#define LED_PATTERN_USB_CONNECTED 1
#define LED_PATTERN_BLE_ADVERTISING 2
#define LED_PATTERN_BLE_CONNECTED 3

// Current LED pattern
static int s_led_pattern = LED_PATTERN_IDLE;

// Neopixel context
static tNeopixelContext *neopixel_ctx = NULL;

// Forward declarations
static void led_control_task(void *arg);
static void init_gpio(void);
static void update_led_pattern(void);

void app_main(void)
{
    ESP_LOGI(TAG, "Starting USB HID to BLE HID bridge");

    // Initialize GPIO
    init_gpio();

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create LED control task
    xTaskCreatePinnedToCore(led_control_task, "LED_Control", 4096, NULL, 5, NULL, 1);

    // Initialize HID bridge
    ret = hid_bridge_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HID bridge: %s", esp_err_to_name(ret));
        return;
    }

    // Start HID bridge
    ret = hid_bridge_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HID bridge: %s", esp_err_to_name(ret));
        return;
    }

    // Main loop
    while (1) {
        // Update LED pattern based on device state
        update_led_pattern();

        // Delay
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void update_led_pattern(void)
{
    // Check USB HID device connection
    bool usb_connected = usb_hid_host_device_connected();
    
    // Check BLE HID device connection
    bool ble_connected = ble_hid_device_connected();

    // Update LED pattern
    if (usb_connected && ble_connected) {
        // Both USB and BLE connected
        s_led_pattern = LED_PATTERN_BLE_CONNECTED;
    } else if (usb_connected) {
        // USB connected, BLE advertising
        s_led_pattern = LED_PATTERN_BLE_ADVERTISING;
    } else if (ble_connected) {
        // BLE connected but no USB device
        s_led_pattern = LED_PATTERN_BLE_CONNECTED;
    } else {
        // Idle
        s_led_pattern = LED_PATTERN_IDLE;
    }
}

static void led_control_task(void *arg)
{
    // Initialize Neopixel
    neopixel_ctx = neopixel_Init(NUM_LEDS, GPIO_WS2812B_PIN);
    if (neopixel_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to initialize NeoPixel");
        vTaskDelete(NULL);
        return;
    }

    // LED animation variables
    int position = 0;
    tNeopixel pixels[NUM_LEDS];

    // Main LED control loop
    while (1) {
        // Clear all pixels
        for (int i = 0; i < NUM_LEDS; i++) {
            pixels[i].index = i;
            pixels[i].rgb = NP_RGB(0, 0, 0);
        }

        // Set pixels based on current pattern
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
                    
                    for (int i = 0; i < NUM_LEDS; i++) {
                        pixels[i].rgb = NP_RGB(0, 0, brightness);
                    }
                }
                break;

            case LED_PATTERN_USB_CONNECTED:
                // Green running light
                for (int i = 0; i < 3; i++) {
                    int pos = (position + i) % NUM_LEDS;
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = NP_RGB(0, intensity, 0);
                }
                position = (position + 1) % NUM_LEDS;
                break;

            case LED_PATTERN_BLE_ADVERTISING:
                // Blue running light
                for (int i = 0; i < 3; i++) {
                    int pos = (position + i) % NUM_LEDS;
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = NP_RGB(0, 0, intensity);
                }
                position = (position + 1) % NUM_LEDS;
                break;

            case LED_PATTERN_BLE_CONNECTED:
                // Purple running light
                for (int i = 0; i < 3; i++) {
                    int pos = (position + i) % NUM_LEDS;
                    uint8_t intensity = 255 >> i;
                    pixels[pos].rgb = NP_RGB(intensity, 0, intensity);
                }
                position = (position + 1) % NUM_LEDS;
                break;

            default:
                break;
        }

        // Update LEDs
        neopixel_SetPixel(neopixel_ctx, pixels, NUM_LEDS);

        // Delay
        vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
    }
}

static void init_gpio(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_NUM_36) |
            (1ULL<<GPIO_NUM_35) |
            (1ULL<<GPIO_NUM_34) |
            (1ULL<<GPIO_NUM_33)
        ),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    // CHARGE_EN: LOW = EN
    gpio_set_level(GPIO_NUM_36, 0);

    // USB_MUX_OE: LOW = EN
    gpio_set_level(GPIO_NUM_34, 0);

    // USB_MUX_SEL: 1 = PC, 0 = MC
    gpio_set_level(GPIO_NUM_33, 0);

    // PWR_LED: red, via 5.1kOhm
    // PWM to optimize battery life
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 32768,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = 35, // GPIO 35
        .duty           = 8, // barely but still visible
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}
