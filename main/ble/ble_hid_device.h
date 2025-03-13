#pragma once

#include <stdbool.h>
#include "esp_err.h"

// Keyboard report structure
typedef struct {
    uint8_t modifier;     // Modifier keys
    uint8_t reserved;     // Reserved byte
    uint8_t keycode[6];   // Array of 6 key codes
} keyboard_report_t;

// Mouse report structure
typedef struct {
    uint8_t buttons;    // Button state
    uint16_t x;        // X movement
    uint16_t y;        // Y movement
    int8_t wheel;      // Wheel movement
} mouse_report_t;

/**
 * @brief Initialize BLE HID device
 * @return ESP_OK on success
 */
esp_err_t ble_hid_device_init(void);

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
