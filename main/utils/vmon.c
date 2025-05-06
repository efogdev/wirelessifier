#include "utils/vmon.h"
#include <ulp.h>
#include <driver/gpio.h>
#include <ulp/ulp_bat.h>
#include "const.h"
#include "utils/rgb_leds.h"
#include "ble_hid_device.h"
#include "utils/adc.h"
#include "esp_log.h"
#include "storage.h"

static const char *TAG = "VMON";
static bool s_psu_connected = false;
static bool s_charging = false;
static bool s_never_wired = false;
static float bat_volts = 0;
static bool s_slow_phase = false;

void enable_no_wire_mode() {
    s_never_wired = true;
}

void set_fast_charging_from_settings() {
    gpio_set_level(GPIO_BAT_CE, 1);

    bool fast_charge;
    if (storage_get_bool_setting("power.fastCharge", &fast_charge) == ESP_OK && fast_charge) {
        ESP_LOGW(TAG, "Fast charging ENABLED!");

        // ±5W
        gpio_set_level(GPIO_BAT_ISET1, 0);
        gpio_set_level(GPIO_BAT_ISET2, 0);
        gpio_set_level(GPIO_BAT_ISET3, 0);
        gpio_set_level(GPIO_BAT_ISET4, 0);
        gpio_set_level(GPIO_BAT_ISET5, 0);
        gpio_set_level(GPIO_BAT_ISET6, 0);
    } else {
        ESP_LOGI(TAG, "Fast charging disabled!");

        // ±2W
        gpio_set_level(GPIO_BAT_ISET1, 1);
        gpio_set_level(GPIO_BAT_ISET2, 1);
        gpio_set_level(GPIO_BAT_ISET3, 1);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(GPIO_BAT_CE, 0);
}

void vmon_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(50));

    uint16_t i = 0;
    while (1) {
        i++;
        bat_volts = (float)adc_read_channel(ADC_CHAN_BAT) * 2 / 1000;
        const float vin_volts = (float)adc_read_channel(ADC_CHAN_VIN) * 2 / 1000;

        if (VERBOSE && i % 20 == 0) {
            ESP_LOGI(TAG, "BAT: %.2fV, VIN: %.2fV", bat_volts, vin_volts, ulp_last_result);
        }

        if (bat_volts < 3.3f && vin_volts < 4.2f) {
            ESP_LOGI(TAG, "Battery is dead. So am I…");
            led_update_status(STATUS_COLOR_RED, STATUS_MODE_ON);
            ble_hid_device_deinit();
            vTaskDelay(pdMS_TO_TICKS(50));
            deep_sleep();
        }

        if (vin_volts > 4.2f && !s_psu_connected) {
            s_psu_connected = true;
            s_slow_phase = false;

            set_fast_charging_from_settings();

            if (!s_never_wired) {
                gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_PC);
            }
        } else if (vin_volts < 4.2f && s_psu_connected) {
            gpio_set_level(GPIO_BAT_CE, 1);
            s_psu_connected = false;
            gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);
        }

        const bool charging = !gpio_get_level(GPIO_BAT_CHRG);
        if (charging != s_charging) {
            s_charging = charging;
        }

        if (s_charging && !s_slow_phase && bat_volts >= 4.05f) {
            ESP_LOGI(TAG, "Vbat_reg is now 4.05V, going into slow charging phase…");

            s_slow_phase = true;
            gpio_set_level(GPIO_BAT_CE, 1);
            gpio_set_level(GPIO_BAT_ISET1, 1);
            gpio_set_level(GPIO_BAT_ISET2, 1);
            gpio_set_level(GPIO_BAT_ISET3, 1);
            gpio_set_level(GPIO_BAT_ISET4, 1);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_set_level(GPIO_BAT_CE, 0);
        }

        if (s_slow_phase && bat_volts < 4.00f) {
            s_slow_phase = false;
            set_fast_charging_from_settings();
        }
        
        vTaskDelay(pdMS_TO_TICKS(128));
    }
}

IRAM_ATTR bool is_charging(void) {
    // return false; // debug
    return s_psu_connected && s_charging;
}

IRAM_ATTR bool is_psu_connected(void) {
    // return false; // debug
    return s_psu_connected;
}

IRAM_ATTR float get_battery_level(void) {
    static float prev_level = 0;
    if (prev_level == 0) {
        prev_level = bat_volts;
        return bat_volts;
    }
    
    if (is_charging()) {
        prev_level = bat_volts > prev_level ? bat_volts : prev_level;
    } else {
        prev_level = bat_volts < prev_level ? bat_volts : prev_level;
    }
    return prev_level;
}

IRAM_ATTR battery_state_t get_battery_state(void) {
    battery_state_t new_state;
    const float level = get_battery_level();

    if (is_charging()) {
        new_state = BATTERY_CHARGING;
    } else if (level > 3.70f || is_psu_connected()) {
        new_state = BATTERY_NORMAL;
    } else if (level > 3.64f) {
        new_state = BATTERY_WARNING;
    } else {
        new_state = BATTERY_LOW;
    }

    return new_state;
}
