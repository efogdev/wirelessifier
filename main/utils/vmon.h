#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "utils/adc.h"

typedef enum {
    BATTERY_CHARGING,
    BATTERY_NORMAL,
    BATTERY_WARNING, 
    BATTERY_LOW
} battery_state_t;

void vmon_task(void *pvParameters);
bool is_psu_connected(void);
bool is_charging(void);
battery_state_t get_battery_state(void);
float get_battery_level(void);
