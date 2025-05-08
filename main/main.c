#include <adc.h>
#include <esp_log.h>
#include <inttypes.h>
#include <const.h>
#include <descriptor_parser.h>
#include <esp_phy_init.h>
#include <stdio.h>
#include <ulp_common.h>
#include <driver/ledc.h>
#include <driver/rtc_io.h>
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
#include "buttons.h"
#include "hid_bridge.h"
#include "utils/rgb_leds.h"
#include "utils/storage.h"
#include "utils/rotary_enc.h"
#include "web/http_server.h"
#include "ulp_bat.h"
#include "ulp/ulp_bat.h"
#include "esp_sleep.h"
#include "wifi_manager.h"

#define MAIN_LOOP_DELAY_MS 35

static const char *TAG = "MAIN";
static QueueHandle_t intrQueue = NULL;
static bool s_web_enabled = false;

static void log_bits(uint32_t value, size_t size);
static void init_variables(void);
static void init_pm(void);
static void init_gpio(void);
static void run_hid_bridge(void);
static void init_web_stack(void);
static void rot_long_press_cb(void);

void app_main(void) {
    if (VERBOSE) {
        ESP_LOGI(TAG, "Starting USB HID to BLE HID bridge");
    }

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
    buttons_init();
    led_control_init(NUM_LEDS, GPIO_WS2812B_PIN);
    descriptor_parser_init();
    run_hid_bridge();

    rotary_enc_subscribe_long_press(rot_long_press_cb);
    xTaskCreatePinnedToCore(vmon_task, "vmon", 2048, NULL, 5, NULL, 1);

    const uint8_t btn2 = gpio_get_level(GPIO_BUTTON_SW2);
    if (!btn2) {
        // if SW2 held on boot, never sleep
        enable_no_sleep_mode();
    }

    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (cause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        ESP_LOGW(TAG, "Woke up, reason=0x%02X, last reading: 0x%04X (%d)", cause, ulp_last_result, ulp_last_result);
        if (cause == ESP_SLEEP_WAKEUP_EXT1) {
            log_bits(esp_sleep_get_ext1_wakeup_status(), 4);
        }

        ulp_last_result = 0;
    } else {
        const uint8_t btn3 = gpio_get_level(GPIO_BUTTON_SW3);
        if (!btn3) {
            // if SW3 held on boot, never switch to wired HID until restart
            enable_no_wire_mode();
        }

        init_web_stack();
    }

    static uint32_t sleep_counter = 0;
    const uint32_t sleep_threshold = (3 * 60 * 1000) / MAIN_LOOP_DELAY_MS; // 3 minutes
    // const uint32_t sleep_threshold = (20 * 1000) / MAIN_LOOP_DELAY_MS; // 20 sec (debug)
    // const uint32_t sleep_threshold = (30 * 60 * 1000) / MAIN_LOOP_DELAY_MS; // 30 min (debug)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(MAIN_LOOP_DELAY_MS));
        led_update_pattern(usb_hid_host_device_connected(), ble_hid_device_connected(), hid_bridge_is_ble_paused());

        if (!is_psu_connected() &&
            !usb_hid_host_device_connected() &&
            !ble_hid_device_connected()) {
            if (++sleep_counter >= sleep_threshold) {
                led_update_pattern(true, true, false); // all black, we good
                hid_bridge_stop();
                ble_hid_device_deinit();
                vTaskDelay(pdMS_TO_TICKS(20));
                ESP_LOGW(TAG, "Entering deep sleep - no devices connected…");
                deep_sleep();
            }
        } else {
            sleep_counter = 0;
        }
    }
}

static void init_variables() {
    intrQueue = xQueueCreate(6, sizeof(int));
}

static void init_pm() {
    const esp_pm_config_t cfg = {
        .light_sleep_enable = false,
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&cfg));
}

static void run_hid_bridge() {
    gpio_set_level(GPIO_5V_EN, 1);
    gpio_set_level(GPIO_MUX_OE, 0);
    gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);

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
    if (gpio_get_level(GPIO_BUTTON_SW4) == 0) {
        vTaskDelay(pdMS_TO_TICKS(20));

        if (VERBOSE) {
            ESP_LOGI(TAG, "Initializing web services because SW4 held on boot");
        }

        s_web_enabled = true;
    } else {
        nvs_handle_t nvs_handle;
        uint8_t boot_with_wifi = 0;
        if (nvs_open("wifi_config", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            if (nvs_get_u8(nvs_handle, "boot_wifi", &boot_with_wifi) == ESP_OK && boot_with_wifi == 1) {
                if (VERBOSE) {
                    ESP_LOGI(TAG, "Initializing web services because of one-time boot flag");
                }

                s_web_enabled = true;
                
                nvs_set_u8(nvs_handle, "boot_wifi", 0);
                nvs_commit(nvs_handle);
            }
            nvs_close(nvs_handle);
        }
    }
    
    if (s_web_enabled) {
        init_web_services();
    }
}

static void init_gpio(void) {
    rtc_gpio_deinit(GPIO_BUTTON_SW1);
    rtc_gpio_deinit(GPIO_BUTTON_SW2);
    rtc_gpio_deinit(GPIO_BUTTON_SW3);
    rtc_gpio_deinit(GPIO_BUTTON_SW4);

    rtc_gpio_pulldown_dis(GPIO_BUTTON_SW1);
    rtc_gpio_pulldown_dis(GPIO_BUTTON_SW2);
    rtc_gpio_pulldown_dis(GPIO_BUTTON_SW3);
    rtc_gpio_pulldown_dis(GPIO_BUTTON_SW4);

    rtc_gpio_pullup_dis(GPIO_BUTTON_SW1);
    rtc_gpio_pullup_dis(GPIO_BUTTON_SW2);
    rtc_gpio_pullup_dis(GPIO_BUTTON_SW3);
    rtc_gpio_pullup_dis(GPIO_BUTTON_SW4);

    rtc_gpio_hold_dis(GPIO_BUTTON_SW1);
    rtc_gpio_hold_dis(GPIO_BUTTON_SW2);
    rtc_gpio_hold_dis(GPIO_BUTTON_SW3);
    rtc_gpio_hold_dis(GPIO_BUTTON_SW4);

    gpio_deep_sleep_hold_dis();
    esp_deep_sleep_disable_rom_logging();
    // esp_sleep_enable_ulp_wakeup();
    esp_sleep_enable_ext0_wakeup(GPIO_ADC_VIN, 1);
    esp_sleep_enable_ext1_wakeup_io(
        (1ULL<<GPIO_BUTTON_SW1) |
        (1ULL<<GPIO_BUTTON_SW2) |
        (1ULL<<GPIO_BUTTON_SW3) |
        (1ULL<<GPIO_BUTTON_SW4),
        ESP_EXT1_WAKEUP_ANY_LOW
    );

    const gpio_config_t output_pullup_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_BAT_CE) |
            (1ULL<<GPIO_5V_EN) |
            (1ULL<<GPIO_MUX_SEL) |
            (1ULL<<GPIO_MUX_OE)
#ifdef HW02
            | (1ULL<<GPIO_ROT_D)
#endif
        ),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&output_pullup_conf);

#ifdef HW02
    const gpio_config_t output_nopull_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_BAT_ISET1)
            | (1ULL<<GPIO_BAT_ISET2)
            | (1ULL<<GPIO_BAT_ISET3)
            | (1ULL<<GPIO_BAT_ISET4)
            | (1ULL<<GPIO_BAT_ISET5)
            | (1ULL<<GPIO_BAT_ISET6)
            | (1ULL<<GPIO_WS2812B_PIN)
        ),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&output_nopull_conf);

    const gpio_config_t input_nopull_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_BUTTON_SW1) |
            (1ULL<<GPIO_BUTTON_SW2) |
            (1ULL<<GPIO_BUTTON_SW3) |
            (1ULL<<GPIO_BUTTON_SW4) |
            (1ULL<<GPIO_ADC_BAT) |
            (1ULL<<GPIO_ADC_VIN)
        ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE
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

    const gpio_config_t input_pullup_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_PGOOD) |
            (1ULL<<GPIO_BAT_CHRG)
        ),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&input_pullup_conf);

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

    gpio_install_isr_service(0);
}

static void rot_long_press_cb(void) {
    rotary_enc_deinit();
    rgb_enter_flash_mode();

    // hold both BTN1 and BTN2 to enter flash mode, otherwise just restart
    const uint8_t btn1 = gpio_get_level(GPIO_BUTTON_SW1);
    const uint8_t btn2 = gpio_get_level(GPIO_BUTTON_SW2);
    if (btn1 == 0 && btn2 == 0) {
        REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    }

    // hold BTN4 to restart with WiFi
    const uint8_t btn4 = gpio_get_level(GPIO_BUTTON_SW4);
    if (btn4 == 0) {
        storage_set_boot_with_wifi();
    } else {
        nvs_handle_t nvs_handle;
        const esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            nvs_set_u8(nvs_handle, NVS_KEY_BOOT_WITH_WIFI, 0);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);

            if (VERBOSE) {
                ESP_LOGI(TAG, "Cleared boot with WiFi flag");
            }
        }
    }

    esp_restart();
}

static void log_bits(const uint32_t value, const size_t size) {
    char bit_str[size * 8 + 1];
    bit_str[0] = '\0';
    
    for (int i = size - 1; i >= 0; i--) {
        const uint8_t byte = (value >> (i * 8)) & 0xFF;
        for (int j = 7; j >= 0; j--) {
            strcat(bit_str, (byte & (1 << j)) ? "1" : "0");
        }
    }
    
    ESP_LOGW(TAG, "EXT1 bitmask: %s", bit_str);
}
