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

    while (1) {
        bat_volts = (float)adc_read_channel(ADC_CHAN_BAT) * 2 / 1000;
        const float vin_volts = (float)adc_read_channel(ADC_CHAN_VIN) * 2 / 1000;

        if (VERBOSE) {
            ESP_LOGI(TAG, "BAT: %.2fV, VIN: %.2fV", bat_volts, vin_volts, ulp_last_result);
        }

        if (bat_volts < 3.3f && vin_volts < 4.5f) {
            ESP_LOGI(TAG, "Battery is dead. So am I…");
            led_update_status(STATUS_COLOR_RED, STATUS_MODE_ON);
            ble_hid_device_deinit();
            vTaskDelay(pdMS_TO_TICKS(50));
            gracefully_die();
        }

        if (vin_volts > 4.5f && !s_psu_connected) {
            gpio_set_level(GPIO_BAT_CE, 0);
            s_psu_connected = true;
#ifdef HW01
            gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_PC);
#elifdef HW02
            gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_PC);
#endif
        } else if (vin_volts < 4.5f && s_psu_connected) {
            gpio_set_level(GPIO_BAT_CE, 1);
            s_psu_connected = false;
#ifdef HW01
            gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);
#elifdef HW02
            gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);
#endif
        }

        s_charging = !gpio_get_level(GPIO_BAT_CHRG);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

bool is_charging(void) {
    return s_psu_connected && s_charging;
}

bool is_psu_connected(void) {
    return s_psu_connected;
}

battery_state_t get_battery_state(void) {
    if (is_charging()) {
        return BATTERY_CHARGING;
    }
    if (is_psu_connected()) {
        return BATTERY_NORMAL;
    }
    if (bat_volts > 3.6f) {
        return BATTERY_NORMAL;
    }
    if (bat_volts > 3.5f) {
        return BATTERY_WARNING;
    }
    return BATTERY_LOW;
}
