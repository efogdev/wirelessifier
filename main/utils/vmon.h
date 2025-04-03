#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "utils/adc.h"

void vmon_task(void *pvParameters);
bool is_psu_connected(void);
