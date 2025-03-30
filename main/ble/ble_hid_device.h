#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    uint8_t modifier;
    uint8_t keycodes[6];
} keyboard_report_t;

typedef struct {
    uint8_t buttons;
    uint16_t x;
    uint16_t y;
    int8_t wheel;
    int8_t pan;
} mouse_report_t;

/**
 * @brief Initialize BLE HID device
 * @return ESP_OK on success
 */
esp_err_t ble_hid_device_init();

/**
 * @brief Deinitialize BLE HID device
 * @return ESP_OK on success
 */
esp_err_t ble_hid_device_deinit(void);

/**
 * @brief Start advertising BLE HID device
 * @return ESP_OK on success
 */
esp_err_t ble_hid_device_start_advertising(void);

/**
 * @brief Check if BLE HID device is connected
 * @return true if connected, false otherwise
 */
bool ble_hid_device_connected(void);

/**
 * @brief Send keyboard report
 * @param report Keyboard report structure
 * @return ESP_OK on success
 */
esp_err_t ble_hid_device_send_keyboard_report(const keyboard_report_t *report);

/**
 * @brief Send mouse report
 * @param report Mouse report structure
 * @return ESP_OK on success
 */
esp_err_t ble_hid_device_send_mouse_report(const mouse_report_t *report);
