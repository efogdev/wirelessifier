#include "ulp.h"
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

void IRAM_ATTR gracefullyDie(void) {
    esp_err_t err = ulp_load_binary(0, ulp_main_bin_start,
            (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
    ESP_ERROR_CHECK(err);

    const ulp_adc_cfg_t cfg = {
        .adc_n    = ADC_UNIT_1,
        .channel  = ADC_CHAN_BAT,
        .width    = ADC_BITWIDTH_12,
        .atten    = ADC_ATTEN_DB_12,
        .ulp_mode = ADC_ULP_MODE_FSM,
    };
    ESP_ERROR_CHECK(ulp_adc_init(&cfg));
    ulp_set_wakeup_period(0, 100000);
    esp_deep_sleep_disable_rom_logging();
    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());

    err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
    ESP_ERROR_CHECK(err);
    esp_deep_sleep_start();
}
