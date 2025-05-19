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
#include "freertos/timers.h"

#define SLOW_PHASE_DURATION_MAX (45 * 60 * 1000)
#define SLOW_PHASE_ENTRY_DELAY (3 * 1000)

#define VIN_THRESHOLD          4.20f
#define BAT_SLOW_PHASE_THRESH  4.16f
#define BAT_NORMAL_THRESH      3.52f
#define BAT_WARNING_THRESH     3.43f
#define BAT_DEAD_THRESH        3.25f
#define ADC_CORRECTION_COEF    1.025f

static const char *TAG = "VMON";
static TimerHandle_t slow_phase_timer = NULL;
static TimerHandle_t slow_phase_entry_timer = NULL;
static bool s_psu_connected = false;
static bool s_charging = false;
static bool s_never_wired = false;
static float bat_volts = 0;
static bool s_slow_phase = false;
static bool s_charging_finished = false;
static bool s_disable_warn = false;

// ToDo remove this and impement proper termination
static void slow_phase_timer_cb(TimerHandle_t xTimer) {
    if (!s_charging_finished && s_slow_phase && s_psu_connected && s_charging && bat_volts >= BAT_SLOW_PHASE_THRESH) {
        ESP_LOGW(TAG, "Timeout reached, terminating charging…");
        s_charging_finished = true;
        gpio_set_level(GPIO_BAT_CE, 1);
    }
}

static void slow_phase_entry_timer_cb(TimerHandle_t xTimer) {
    if (s_charging && !s_slow_phase && bat_volts >= BAT_SLOW_PHASE_THRESH) {
        ESP_LOGW(TAG, "Vbat ≥ %.2fV, going into slow charging phase…", BAT_SLOW_PHASE_THRESH);

        if (!slow_phase_timer) {
            slow_phase_timer = xTimerCreate("slow_phase_timer", pdMS_TO_TICKS(SLOW_PHASE_DURATION_MAX), pdFALSE, NULL, slow_phase_timer_cb);
            xTimerStart(slow_phase_timer, 0);
        }

        // isn't really "slow", just a workaround
        // because of charging IC specifics (Iterm = Iset/10)
        s_slow_phase = true;
        gpio_set_level(GPIO_BAT_CE, 1);
        gpio_set_level(GPIO_BAT_ISET1, 1);
        gpio_set_level(GPIO_BAT_ISET2, 0);
        gpio_set_level(GPIO_BAT_ISET3, 0);
        gpio_set_level(GPIO_BAT_ISET4, 0);
        gpio_set_level(GPIO_BAT_ISET5, 0);
        gpio_set_level(GPIO_BAT_ISET6, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(GPIO_BAT_CE, 0);
    }
}

void enable_no_wire_mode() {
    s_never_wired = true;
}

static void start_charging() {
    gpio_set_level(GPIO_BAT_CE, 1);

    static bool fast_charge;
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
        if (VERBOSE) {
            ESP_LOGI(TAG, "Fast charging disabled!");
        }

        // ±2.5W
        gpio_set_level(GPIO_BAT_ISET1, 1);
        gpio_set_level(GPIO_BAT_ISET2, 1);
        gpio_set_level(GPIO_BAT_ISET3, 0);
        gpio_set_level(GPIO_BAT_ISET4, 0);
        gpio_set_level(GPIO_BAT_ISET5, 0);
        gpio_set_level(GPIO_BAT_ISET6, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(GPIO_BAT_CE, 0);
}

void vmon_task(void *pvParameters) {
    bool disable_slow_phase;
    bool fast_charge;

    storage_get_bool_setting("power.disableSlowPhase", &disable_slow_phase);
    storage_get_bool_setting("power.fastCharge", &fast_charge);
    storage_get_bool_setting("power.disableWarn", &s_disable_warn);
    vTaskDelay(pdMS_TO_TICKS(50));

    uint16_t i = 0;
    while (1) {
        // I'm still unsure why ADC readings are incorrect and a magic coef is required
        // but the offset seems pretty consistent across different PCBs and batteries
        bat_volts = ((float)adc_read_channel(ADC_CHAN_BAT) * 2) / 1000 * ADC_CORRECTION_COEF;
        const float vin_volts = ((float)adc_read_channel(ADC_CHAN_VIN) * 2) / 1000 * ADC_CORRECTION_COEF;

        if (VERBOSE && ++i % 10 == 0) {
            ESP_LOGI(TAG, "BAT: %.3fV, Vin: %.3fV", bat_volts, vin_volts);
        }

        if (bat_volts < BAT_DEAD_THRESH && vin_volts < VIN_THRESHOLD) {
            if (VERBOSE) {
                ESP_LOGI(TAG, "Battery is dead. So am I…");
            }

            led_update_status(STATUS_COLOR_RED, STATUS_MODE_ON);
            ble_hid_device_deinit();
            vTaskDelay(pdMS_TO_TICKS(50));
            deep_sleep();
        }

        if (vin_volts > VIN_THRESHOLD && !s_psu_connected) {
            s_psu_connected = true;
            s_charging_finished = false;
            s_slow_phase = false;

            start_charging();
            if (!s_never_wired) {
                if (VERBOSE) {
                    ESP_LOGI(TAG, "USB data lines switched to male port.");
                }

                gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_PC);
            } else {
                if (VERBOSE) {
                    ESP_LOGI(TAG, "USB data lines switched to MCU.");
                }

                gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);
            }
        } else if (vin_volts < VIN_THRESHOLD && s_psu_connected) {
            s_psu_connected = false;
            s_charging_finished = false;
            s_slow_phase = false;

            if (slow_phase_timer) {
                xTimerStop(slow_phase_timer, 0);
                xTimerDelete(slow_phase_timer, 0);
                slow_phase_timer = NULL;
            }

            if (slow_phase_entry_timer) {
                xTimerStop(slow_phase_entry_timer, 0);
                xTimerDelete(slow_phase_entry_timer, 0);
                slow_phase_entry_timer = NULL;
            }

            gpio_set_level(GPIO_BAT_CE, 1);
            gpio_set_level(GPIO_MUX_SEL, GPIO_MUX_SEL_MC);
        }

        s_charging = !gpio_get_level(GPIO_BAT_CHRG);
        if (s_charging_finished) {
            continue;
        }

        if (s_slow_phase && s_psu_connected && !s_charging) {
            ESP_LOGW(TAG, "Charging finished!");

            s_charging_finished = true;
            gpio_set_level(GPIO_BAT_CE, 1);
            continue;
        }

        if (s_charging && !s_slow_phase && bat_volts >= BAT_SLOW_PHASE_THRESH && (!disable_slow_phase || !fast_charge)) {
            if (!slow_phase_entry_timer) {
                slow_phase_entry_timer = xTimerCreate("slow_phase_entry_timer", pdMS_TO_TICKS(SLOW_PHASE_ENTRY_DELAY), pdFALSE, NULL, slow_phase_entry_timer_cb);
                xTimerStart(slow_phase_entry_timer, 0);
            }
        } else if (bat_volts < BAT_SLOW_PHASE_THRESH && slow_phase_entry_timer) {
            xTimerStop(slow_phase_entry_timer, 0);
            xTimerDelete(slow_phase_entry_timer, 0);
            slow_phase_entry_timer = NULL;
        }
        
        vTaskDelay(pdMS_TO_TICKS(i == 1 ? 1 : 128));
    }
}

IRAM_ATTR bool is_charging(void) {
    return s_psu_connected && s_charging;
}

IRAM_ATTR bool is_psu_connected(void) {
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
    } else if (level > (s_disable_warn ? BAT_WARNING_THRESH : BAT_NORMAL_THRESH) || is_psu_connected()) {
        new_state = BATTERY_NORMAL;
    } else if (level > BAT_WARNING_THRESH && !s_disable_warn) {
        new_state = BATTERY_WARNING;
    } else {
        new_state = BATTERY_LOW;
    }

    return new_state;
}
