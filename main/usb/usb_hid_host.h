#pragma once

// Utility macro for getting minimum of two values
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hid_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

// HID Report Items from HID 1.11 Section 6.2.2
#define HID_USAGE_PAGE      0x05
#define HID_USAGE           0x09
#define HID_COLLECTION      0xA1
#define HID_END_COLLECTION  0xC0
#define HID_REPORT_COUNT    0x95
#define HID_REPORT_SIZE     0x75
#define HID_USAGE_MIN       0x19
#define HID_USAGE_MAX       0x29
#define HID_LOGICAL_MIN     0x15
#define HID_LOGICAL_MIN_2   0x16 // 2-byte data
#define HID_LOGICAL_MAX     0x25
#define HID_LOGICAL_MAX_2   0x26 // 2-byte data
#define HID_INPUT           0x81
#define HID_OUTPUT          0x91
#define HID_FEATURE         0xB1

typedef void (*usb_hid_report_callback_t)(usb_hid_report_t *report);

/**
 * @brief Initialize the USB HID Host* 
 * @param report_queue Queue to receive HID reports
 * @param verbose
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t usb_hid_host_init(QueueHandle_t report_queue, bool verbose);

/**
 * @brief Deinitialize the USB HID Host
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t usb_hid_host_deinit(void);

/**
 * @brief Check if a USB HID device is connected
 * 
 * This function returns the current connection status of USB HID devices.
 * 
 * @return true if a USB HID device is connected, false otherwise
 */
bool usb_hid_host_device_connected(void);

/**
 * @brief Get the number of fields for a given report ID
 * @param report_id Report ID to look up
 * @param interface_num Interface number
 * @return Number of fields, or 0 if report ID not found
 */
uint8_t usb_hid_host_get_num_fields(uint8_t report_id, uint8_t interface_num);

#ifdef __cplusplus
}
#endif
