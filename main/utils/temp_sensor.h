#pragma once

#include "esp_err.h"
#include "driver/temperature_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the temperature sensor
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t temp_sensor_init(void);

/**
 * @brief Get the current temperature in Celsius
 * 
 * @param temperature Pointer to store the temperature value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t temp_sensor_get_temperature(float *temperature);

/**
 * @brief Deinitialize the temperature sensor
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t temp_sensor_deinit(void);

#ifdef __cplusplus
}
#endif
