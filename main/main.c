#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "usb_hid_host.h"
#include "ble_hid_device.h"
#include "hid_bridge.h"
#include "rgb_utils.h"

static const char *TAG = "MAIN";

#define GPIO_RESET_PIN GPIO_NUM_16
#define GPIO_WS2812B_PIN GPIO_NUM_38
#define GPIO_BUTTON_SW4 GPIO_NUM_13
#define NUM_LEDS 17

static TaskHandle_t button_task_handle = NULL;
static QueueHandle_t intrQueue = NULL;

static void button_task(void *arg);
static void init_gpio(void);

static void IRAM_ATTR gpio_button_isr_handler(void* args) {
    int arg = (int) args;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    xQueueSendFromISR(intrQueue, &arg, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void init_variables() {
    intrQueue = xQueueCreate(4, sizeof(int));
}

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
    led_control_init(NUM_LEDS, GPIO_WS2812B_PIN);
    xTaskCreate(button_task, "button_task", 8192 * 4, NULL, 2, &button_task_handle);

    while (1) {
        led_update_pattern(usb_hid_host_device_connected(), ble_hid_device_connected());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
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

static void button_task(void *arg) {
    int value;
    while (1) {
        if (xQueueReceive(intrQueue, &value, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Button pressed, starting HID bridge");
            run_hid_bridge();
        }
    }
}

static void init_gpio(void) {
    gpio_config_t io_conf = {
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
    gpio_config(&io_conf);

    gpio_config_t input_nopull_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_NUM_19) |
            (1ULL<<GPIO_NUM_20)
        ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&input_nopull_conf);

    gpio_config_t input_pullup_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_NUM_14) |
            (1ULL<<GPIO_NUM_15) |
            (1ULL<<GPIO_NUM_16)
        ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&input_pullup_conf);

    gpio_config_t button_conf = {
        .pin_bit_mask = (1ULL<<GPIO_BUTTON_SW4),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE 
    };
    gpio_config(&button_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_BUTTON_SW4, gpio_button_isr_handler, (void*) 1);

    // CHARGE_EN: LOW = EN
    gpio_set_level(GPIO_NUM_36, 1);

    // USB_MUX_OE: LOW = EN
    gpio_set_level(GPIO_NUM_34, 1);

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
        .duty           = 32, // low brightness but visible
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}
