#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "usb_hid_host.h"

/**
 * @brief Consumer control commands
 */
typedef enum {
    HID_CONSUMER_CHANNEL_UP = 1,
    HID_CONSUMER_CHANNEL_DOWN,
    HID_CONSUMER_VOLUME_UP,
    HID_CONSUMER_VOLUME_DOWN,
    HID_CONSUMER_MUTE,
    HID_CONSUMER_POWER,
    HID_CONSUMER_RECALL_LAST,
    HID_CONSUMER_ASSIGN_SEL,
    HID_CONSUMER_PLAY,
    HID_CONSUMER_PAUSE,
    HID_CONSUMER_RECORD,
    HID_CONSUMER_FAST_FORWARD,
    HID_CONSUMER_REWIND,
    HID_CONSUMER_SCAN_NEXT_TRK,
    HID_CONSUMER_SCAN_PREV_TRK,
    HID_CONSUMER_STOP
} consumer_cmd_t;

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

/**
 * @brief Send consumer control command over BLE HID
 * 
 * @param cmd Consumer control command to send
 * @param pressed true if button is pressed, false if released
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ble_hid_device_send_consumer_control(consumer_cmd_t cmd, bool pressed);

#ifdef __cplusplus
}
#endif
