#include "utils/vmon.h"
#include <ulp.h>
#include <driver/gpio.h>
#include <ulp/ulp_bat.h>
#include "const.h"
#include "utils/rgb_leds.h"
#include "ble_hid_device.h"
#include "utils/adc.h"
#include "esp_log.h"

static const char *TAG = "VMON";
static bool s_psu_connected = false;
static bool s_charging = false;
static float bat_volts = 0;

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
            gpio_set_level(GPIO_BAT_CE, 0);
            s_psu_connected = true;
            gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_PC);
        } else if (vin_volts < 4.2f && s_psu_connected) {
            gpio_set_level(GPIO_BAT_CE, 1);
            s_psu_connected = false;
            gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);
        }

        s_charging = !gpio_get_level(GPIO_BAT_CHRG);
        if (s_charging) {
            bat_volts = bat_volts - 0.2f;
        }

        vTaskDelay(pdMS_TO_TICKS(128));
    }
}

bool is_charging(void) {
    // return false; // debug
    return s_psu_connected && s_charging;
}

bool is_psu_connected(void) {
    // return false; // debug
    return s_psu_connected;
}

float get_battery_level(void) {
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

battery_state_t get_battery_state(void) {
    battery_state_t new_state;

    if (is_charging()) {
        new_state = BATTERY_CHARGING;
    } else if (get_battery_level() > 3.75f || is_psu_connected()) {
        new_state = BATTERY_NORMAL;
    } else if (get_battery_level() > 3.65f) {
        new_state = BATTERY_WARNING;
    } else {
        new_state = BATTERY_LOW;
    }

    return new_state;
}
