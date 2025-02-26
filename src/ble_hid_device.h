#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "usb_hid_host.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize BLE HID device
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ble_hid_device_init(void);

/**
 * @brief Deinitialize BLE HID device
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ble_hid_device_deinit(void);

/**
 * @brief Start advertising BLE HID device
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ble_hid_device_start_advertising(void);

/**
 * @brief Stop advertising BLE HID device
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ble_hid_device_stop_advertising(void);

/**
 * @brief Check if BLE HID device is connected
 * 
 * @return true if connected, false otherwise
 */
bool ble_hid_device_connected(void);

/**
 * @brief Send keyboard report over BLE HID
 * 
 * @param keyboard_report Keyboard report to send
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ble_hid_device_send_keyboard_report(usb_hid_keyboard_report_t *keyboard_report);

/**
 * @brief Send mouse report over BLE HID
 * 
 * @param mouse_report Mouse report to send
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ble_hid_device_send_mouse_report(usb_hid_mouse_report_t *mouse_report);

#ifdef __cplusplus
}
#endif
