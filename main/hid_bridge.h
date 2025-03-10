#pragma once

#include "esp_err.h"
#include "usb/usb_hid_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize HID bridge
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hid_bridge_init(void);

/**
 * @brief Deinitialize HID bridge
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hid_bridge_deinit(void);

/**
 * @brief Start HID bridge
 * 
 * This function starts the HID bridge task that forwards USB HID reports to BLE HID
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hid_bridge_start(void);

/**
 * @brief Stop HID bridge
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hid_bridge_stop(void);

/**
 * @brief Process a USB HID report and forward it to BLE HID
 * 
 * @param report USB HID report to process
 * @return esp_err_t ESP_OK on success
 */
esp_err_t hid_bridge_process_report(usb_hid_report_t *report);

#ifdef __cplusplus
}
#endif
