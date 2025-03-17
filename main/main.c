#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "usb/usb_hid_host.h"
#include "ble_hid_device.h"
#include "hid_bridge.h"
#include "rgb/rgb_utils.h"
#include "utils/task_monitor.h"
#include "utils/storage.h"
#include "web/http_server.h"

static const char *TAG = "MAIN";

#define GPIO_RESET_PIN GPIO_NUM_16
#define GPIO_WS2812B_PIN GPIO_NUM_38
#define GPIO_BUTTON_SW4 GPIO_NUM_13
#define NUM_LEDS 17

static QueueHandle_t intrQueue = NULL;

static void init_variables(void);
static void init_pm(void);
static void init_gpio(void);
static void run_hid_bridge(void);
static void init_web_stack(void);

void app_main(void) {
    ESP_LOGI(TAG, "Starting USB HID to BLE HID bridge");
    
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_variables();
    init_gpio();
    init_global_settings();
    init_pm();

    led_control_init(NUM_LEDS, GPIO_WS2812B_PIN);
    led_update_pattern(usb_hid_host_device_connected(), ble_hid_device_connected(), hid_bridge_is_ble_paused());

    ESP_ERROR_CHECK(task_monitor_init());
    ESP_ERROR_CHECK(task_monitor_start());

    run_hid_bridge();
    init_web_stack();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(50));
        led_update_pattern(usb_hid_host_device_connected(), ble_hid_device_connected(), hid_bridge_is_ble_paused());
    }
}

static void init_variables() {
    intrQueue = xQueueCreate(4, sizeof(int));
}

static void init_pm() {
    const esp_pm_config_t cfg = {
        .light_sleep_enable = true,
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&cfg));
}

static void run_hid_bridge() {
    gpio_set_level(GPIO_NUM_34, 0);
    gpio_set_level(GPIO_NUM_33, 0);

    esp_err_t ret = hid_bridge_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HID bridge: %s", esp_err_to_name(ret));
        return;
    }

    ret = hid_bridge_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HID bridge: %s", esp_err_to_name(ret));
        return;
    }
}

static void init_web_stack(void) {
    bool start_web_services = false;
    
    // Check if SW4 button is pressed
    if (gpio_get_level(GPIO_BUTTON_SW4) == 0) {
        vTaskDelay(pdMS_TO_TICKS(60));
        ESP_LOGI(TAG, "Initializing web services because SW4 held on boot");
        start_web_services = true;
    } else {
        // Check for one-time boot flag
        nvs_handle_t nvs_handle;
        uint8_t boot_with_wifi = 0;
        if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            if (nvs_get_u8(nvs_handle, "boot_wifi", &boot_with_wifi) == ESP_OK && boot_with_wifi == 1) {
                ESP_LOGI(TAG, "Initializing web services because of one-time boot flag");
                start_web_services = true;
                
                // Clear the flag since it's one-time
                nvs_set_u8(nvs_handle, "boot_wifi", 0);
                nvs_commit(nvs_handle);
            }
            nvs_close(nvs_handle);
        }
    }
    
    if (start_web_services) {
        init_web_services();
    }
}

static void init_gpio(void) {
    gpio_config_t output_pullup_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_NUM_36) |
            (1ULL<<GPIO_NUM_35) |
            (1ULL<<GPIO_NUM_38) |
            (1ULL<<GPIO_NUM_34) |
            (1ULL<<GPIO_NUM_33)
        ),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&output_pullup_conf);

    gpio_config_t input_pullup_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_NUM_14) |
            (1ULL<<GPIO_NUM_15) |
            (1ULL<<GPIO_BUTTON_SW4)
        ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&input_pullup_conf);

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
        .duty           = 32, // low brightness but visible
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}
