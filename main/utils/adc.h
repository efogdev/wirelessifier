#ifndef ADC_H
#define ADC_H

#include <esp_err.h>

/**
 * @brief Initialize ADC for battery and VIN voltage measurements
 * 
 * @return ESP_OK on success
 */
esp_err_t adc_init(void);

/**
 * @brief Get battery voltage in millivolts
 * 
 * @return Battery voltage in mV
 */
uint32_t adc_get_battery_mv(void);

/**
 * @brief Get VIN voltage in millivolts
 * 
 * @return VIN voltage in mV
 */
uint32_t adc_get_vin_mv(void);

#endif // ADC_H
