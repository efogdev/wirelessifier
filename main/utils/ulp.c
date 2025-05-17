#include "ulp.h"
#include <adc.h>
#include <const.h>
#include "ulp_adc.h"
// #include "ulp_bat.h"
#include "ulp/ulp_bat.h"
#include <esp_err.h>
#include <esp_sleep.h>
#include <rotary_enc.h>
#include <ulp_common.h>
#include <ulp_fsm_common.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_bat_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_bat_bin_end");

void IRAM_ATTR deep_sleep(void) {
    // const ulp_adc_cfg_t cfg = {
        // .adc_n    = ADC_UNIT_1,
        // .channel  = ADC_CHAN_VIN,
        // .width    = ADC_BITWIDTH_12,
        // .atten    = ADC_ATTEN_DB_12,
        // .ulp_mode = ADC_ULP_MODE_FSM,
    // };

    // rotary_enc_deinit();
    // adc_deinit();
    // gpio_uninstall_isr_service();

    // ulp_load_binary(0, ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
    // ulp_set_wakeup_period(0, 100000);
    // ulp_adc_init(&cfg);
    // ulp_run(&ulp_entry - RTC_SLOW_MEM);
    // ulp_timer_resume();

    rotary_enc_deinit();
    rtc_gpio_pullup_en(GPIO_ROT_B);
    rtc_gpio_pulldown_en(GPIO_ROT_A);
    rtc_gpio_init(GPIO_ROT_D);
    rtc_gpio_set_level(GPIO_ROT_D, 0);
    rtc_gpio_pulldown_en(GPIO_ROT_D);
    rtc_gpio_hold_en(GPIO_ROT_D);
    rtc_gpio_pullup_en(GPIO_ROT_E);
    esp_deep_sleep_start();
}
