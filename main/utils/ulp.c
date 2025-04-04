#include "ulp.h"

#include <adc.h>
#include <const.h>
#include "ulp_adc.h"
#include "ulp_bat.h"
#include "ulp/ulp_bat.h"
#include <esp_err.h>
#include <esp_sleep.h>
#include <ulp_common.h>
#include <ulp_fsm_common.h>

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_bat_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_bat_bin_end");

void IRAM_ATTR gracefully_die(void) {
    const ulp_adc_cfg_t cfg = {
        .adc_n    = ADC_UNIT_1,
        .channel  = ADC_CHAN_BAT,
        .width    = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
        .ulp_mode = ADC_ULP_MODE_FSM,
    };

    adc_deinit();
    ulp_timer_resume();
    ulp_load_binary(0, ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
    ulp_adc_init(&cfg);
    ulp_set_wakeup_period(0, 100000);
    esp_deep_sleep_disable_rom_logging();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_enable_ulp_wakeup();

    ulp_check_vin_only = 0;
    ulp_run(&ulp_entry - RTC_SLOW_MEM);
    esp_deep_sleep_start();
}

void IRAM_ATTR deep_sleep(void) {
    const ulp_adc_cfg_t cfg = {
        .adc_n    = ADC_UNIT_1,
        .channel  = ADC_CHAN_VIN,
        .width    = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
        .ulp_mode = ADC_ULP_MODE_FSM,
    };

    adc_deinit();
    ulp_timer_resume();
    ulp_load_binary(0, ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
    ulp_adc_init(&cfg);
    esp_deep_sleep_disable_rom_logging();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_enable_ulp_wakeup();

    ulp_check_vin_only = 1;
    ulp_run(&ulp_entry - RTC_SLOW_MEM);
    esp_deep_sleep_start();
}
