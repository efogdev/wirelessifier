#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"
#include "soc/sens_reg.h"

    .bss
    .global last_result
last_result:
    .long 0

    .global check_vin_only
check_vin_only:
    .long 0

    .text
    .global entry
entry:
    /* check if we should skip to VIN measurement */
    move r3, check_vin_only
    ld r0, r3, 0
    and r0, r0, 1
    jump measure_vin, eq

    /* measure BAT */
    adc r0, 0, 5

    /* store result */
    move r3, last_result
    st r0, r3, 0

    /* compare with threshold */
    move r2, 0x76c
    sub r2, r2, r0
    jump wake_up, ov

measure_vin:
    /* measure VIN */
    adc r0, 0, 6

    /* store result */
    move r3, last_result
    st r0, r3, 0

    /* compare with threshold */
    move r2, 0x898
    sub r2, r2, r0
    jump wake_up, ov

    /* values below threshold, end program */
    .global exit
exit:
    halt

    .global wake_up
wake_up:
    /* Check if the system can be woken up */
    READ_RTC_FIELD(RTC_CNTL_LOW_POWER_ST_REG, RTC_CNTL_RDY_FOR_WAKEUP)
    and r0, r0, 1
    jump wake_up, eq

    /* Wake up the SoC, end program */
    wake
    WRITE_RTC_FIELD(RTC_CNTL_STATE0_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)
    WRITE_RTC_FIELD(RTC_CNTL_ULP_CP_TIMER_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)
    halt
