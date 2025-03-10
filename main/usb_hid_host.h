/**
 * @file usb_hid_host.h
 * @brief USB HID Host interface
 * 
 * This header file defines the interface for the USB HID Host functionality.
 * It provides functions to initialize and deinitialize the USB HID Host,
 * check device connection status, and defines the structures for HID reports.
 * 
 * The implementation follows the architecture described in the USB Host Library documentation,
 * with a Host Library Daemon Task and a Client Task for handling USB events.
 */

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

#ifdef __cplusplus
extern "C" {
#endif

// HID report field types
typedef enum {
    USB_HID_FIELD_TYPE_INPUT = 0,
    USB_HID_FIELD_TYPE_OUTPUT = 1,
    USB_HID_FIELD_TYPE_FEATURE = 2
} usb_hid_field_type_t;

// HID report field attributes
typedef struct {
    uint16_t usage_page;
    uint16_t usage;
    uint8_t report_size;    // Size in bits
    uint8_t report_count;   // Number of data fields
    uint32_t logical_min;
    uint32_t logical_max;
    bool constant;          // Constant vs Data
    bool variable;          // Variable vs Array
    bool relative;          // Relative vs Absolute
} usb_hid_field_attr_t;

// HID report field data
typedef struct {
    usb_hid_field_attr_t attr;
    uint32_t *values;       // Array of values for this field
} usb_hid_field_t;

// HID report structure
typedef struct {
    uint8_t report_id;
    usb_hid_field_type_t type;
    uint8_t num_fields;
    usb_hid_field_t *fields;
    uint8_t raw[64];       // Raw report data
    uint16_t raw_len;      // Length of raw data
} usb_hid_report_t;

// Callback function type for HID reports
typedef void (*usb_hid_report_callback_t)(usb_hid_report_t *report);

/**
 * @brief Initialize the USB HID Host* 
 * @param report_queue Queue to receive HID reports
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t usb_hid_host_init(QueueHandle_t report_queue);

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

#ifdef __cplusplus
}
#endif
