#include "sdkconfig.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/rtc_io_reg.h"
#include "soc/soc_ulp.h"
#include "soc/sens_reg.h"

    .bss
    .global last_result
last_result:
    .long 0

    .text
    .global entry
entry:
    /* measure BAT */
    adc r0, 0, 5
    
    /* store result */
    move r3, last_result
    st r0, r3, 0

    /* compare with threshold */
    move r2, 1900
    sub r2, r2, r0
    jump wake_up, ov

    /* measure VIN */
    adc r0, 0, 6

    /* store result */
    move r3, last_result
    st r0, r3, 0

    /* compare with threshold */
    move r2, 2200
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
    jump exit, eq

    /* Wake up the SoC, end program */
    wake
    WRITE_RTC_FIELD(RTC_CNTL_STATE0_REG, RTC_CNTL_ULP_CP_SLP_TIMER_EN, 0)
    halt
