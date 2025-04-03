#include <adc.h>
#include <esp_log.h>
#include <inttypes.h>
#include <const.h>
#include <descriptor_parser.h>
#include <esp_phy_init.h>
#include <stdio.h>
#include <limits.h>
#include <driver/ledc.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_private/system_internal.h>
#include <hal/usb_wrap_hal.h>
#include <soc/rtc_cntl_reg.h>
#include <utils/ulp.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "utils/vmon.h"
#include "esp_pm.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "usb/usb_hid_host.h"
#include "ble_hid_device.h"
#include "hid_bridge.h"
#include "utils/rgb_leds.h"
#include "utils/storage.h"
#include "utils/rotary_enc.h"
#include "web/http_server.h"
#include "ulp/ulp_bat.h"

static const char *TAG = "MAIN";
static QueueHandle_t intrQueue = NULL;

static void init_variables(void);
static void init_pm(void);
static void init_gpio(void);
static void run_hid_bridge(void);
static void init_web_stack(void);
static void rot_long_press_cb(void);
static void rot_press_cb(void);

void app_main(void) {
    ESP_LOGI(TAG, "Starting USB HID to BLE HID bridge");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    init_variables();
    init_global_settings();
    init_pm();
    init_gpio();

    adc_init();
    rotary_enc_init();
    led_control_init(NUM_LEDS, GPIO_WS2812B_PIN);
    descriptor_parser_init();

    rotary_enc_subscribe_long_press(rot_long_press_cb);
    rotary_enc_subscribe_click(rot_press_cb);

    run_hid_bridge();
    init_web_stack();

    xTaskCreatePinnedToCore(vmon_task, "vmon", 2048, NULL, 5, NULL, 1);
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(35));
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
    gpio_set_level(GPIO_5V_EN, 1);
    gpio_set_level(GPIO_MUX_OE, 0);

#ifdef HW01
    gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);
#elifdef HW02
    gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);
#endif

    esp_err_t ret = hid_bridge_init(VERBOSE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize HID bridge: %s", esp_err_to_name(ret));
        return;
    }

    ret = hid_bridge_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HID bridge: %s", esp_err_to_name(ret));
    }
}

static void init_web_stack(void) {
    bool start_web_services = false;
    
    if (gpio_get_level(GPIO_BUTTON_SW4) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
        ESP_LOGI(TAG, "Initializing web services because SW4 held on boot");
        start_web_services = true;
    } else {
        nvs_handle_t nvs_handle;
        uint8_t boot_with_wifi = 0;
        if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            if (nvs_get_u8(nvs_handle, "boot_wifi", &boot_with_wifi) == ESP_OK && boot_with_wifi == 1) {
                ESP_LOGI(TAG, "Initializing web services because of one-time boot flag");
                start_web_services = true;
                
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
    const gpio_config_t output_pullup_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_BAT_CE) |
            (1ULL<<GPIO_5V_EN) |
            (1ULL<<GPIO_WS2812B_PIN) |
            (1ULL<<GPIO_MUX_SEL) |
            (1ULL<<GPIO_MUX_OE)
#ifdef HW02
            | (1ULL<<GPIO_BAT_ISET1)
            | (1ULL<<GPIO_BAT_ISET2)
            | (1ULL<<GPIO_BAT_ISET3)
            | (1ULL<<GPIO_BAT_ISET4)
            | (1ULL<<GPIO_BAT_ISET5)
            | (1ULL<<GPIO_BAT_ISET6)
            | (1ULL<<GPIO_ROT_D)
#endif
        ),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&output_pullup_conf);

    const gpio_config_t input_pullup_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_BUTTON_SW4) |
            (1ULL<<GPIO_BUTTON_SW3) |
            (1ULL<<GPIO_BUTTON_SW2) |
            (1ULL<<GPIO_BUTTON_SW1) |
            (1ULL<<GPIO_PGOOD) |
            (1ULL<<GPIO_BAT_CHRG)
        ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&input_pullup_conf);

#ifdef HW02
    const gpio_config_t input_nopull_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_ADC_BAT) |
            (1ULL<<GPIO_ADC_VIN) |
            (1ULL<<GPIO_ROT_A) |
            (1ULL<<GPIO_ROT_B)
        ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&input_nopull_conf);

    const gpio_config_t rot_conf = {
        .pin_bit_mask = (1ULL << GPIO_ROT_A) | (1ULL << GPIO_ROT_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&rot_conf);

    const gpio_config_t rot_btn_conf = {
        .pin_bit_mask = (1ULL << GPIO_ROT_E),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&rot_btn_conf);
    gpio_set_level(GPIO_ROT_D, 1);
#endif

#ifdef HW01
    // PWR_LED: red, via 5.1kOhm
    // PWM to optimize battery life
    const ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 32768,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    const ledc_channel_config_t ledc_channel = {
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
#endif

    float voltage;
    esp_err_t err;
    if ((err = storage_get_float_setting("power.output", &voltage) == ESP_OK)) {
        ESP_LOGW(TAG, "Desired output voltage: %.02fV", voltage);
        if (voltage >= 4.19 && voltage < 5.01) {
            const int max_duty_dif = 300;
            const float duty = 1023 - (max_duty_dif - (voltage - 4.2) * 1.25 * max_duty_dif);

            // PWM for 5V_EN
            // duty >=720 works for Adept
            if (duty > 1023 || duty < 680) {
                ESP_LOGW(TAG, "Unexpected duty cycle: %.02f", duty);
                return;
            }

            ESP_LOGI(TAG, "Duty cycle: %.02f", duty);

            ledc_timer_config_t ledc_timer = {
                .speed_mode       = LEDC_LOW_SPEED_MODE,
                .duty_resolution  = LEDC_TIMER_10_BIT,
                .timer_num        = LEDC_TIMER_0,
                .freq_hz          = 5000,
                .clk_cfg          = LEDC_AUTO_CLK
            };
            ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

            ledc_channel_config_t ledc_channel = {
                .speed_mode     = LEDC_LOW_SPEED_MODE,
                .channel        = LEDC_CHANNEL_0,
                .timer_sel      = LEDC_TIMER_0,
                .intr_type      = LEDC_INTR_DISABLE,
                .gpio_num       = GPIO_5V_EN,
                .duty           = (int) duty, // 100%
                .hpoint         = 0
            };
            ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
        }
    } else {
        ESP_LOGE(TAG, "Failed to get output voltage: %s", esp_err_to_name(err));
    }
}


static void rot_press_cb(void) {
    storage_set_boot_with_wifi();
    esp_restart();
}

static void rot_long_press_cb(void) {
    rotary_enc_deinit();
    rgb_enter_flash_mode();
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    esp_restart();
}
