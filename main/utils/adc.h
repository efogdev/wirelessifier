#ifndef ADC_H
#define ADC_H

#include <esp_err.h>
#include <hal/adc_types.h>

/**
 * @brief Initialize ADC for battery and VIN voltage measurements
 * 
 * @return ESP_OK on success
 */
esp_err_t adc_init(void);

/**
 * @brief Deinitialize ADC
 */
void adc_deinit(void);

/**
 * @brief Get voltage by ADC channel (in millivolts)
 * 
 * @return Voltage in mV, multisampled
 */
uint32_t adc_read_channel(adc_channel_t chan);

#endif // ADC_H
