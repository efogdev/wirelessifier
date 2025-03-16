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

/**
 * @brief Increment the USB HID report counter
 * 
 * This function is called each time a USB HID report is received
 */
void task_monitor_increment_usb_report_counter(void);

#ifdef __cplusplus
}
#endif
