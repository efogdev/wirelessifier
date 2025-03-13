#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize task monitoring system
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_monitor_init(void);

/**
 * @brief Start the monitoring task
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t task_monitor_start(void);

#ifdef __cplusplus
}
#endif
