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

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Custom HID report types for our application
typedef enum {
    USB_HID_REPORT_TYPE_KEYBOARD,
    USB_HID_REPORT_TYPE_MOUSE,
    USB_HID_REPORT_TYPE_UNKNOWN
} usb_hid_report_type_t;

// Keyboard report structure
typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keys[6];
} usb_hid_keyboard_report_t;

// Mouse report structure
typedef struct {
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
} usb_hid_mouse_report_t;

// Generic HID report structure
typedef struct {
    usb_hid_report_type_t type;
    union {
        usb_hid_keyboard_report_t keyboard;
        usb_hid_mouse_report_t mouse;
        uint8_t raw[64];
    };
    uint16_t raw_len;
} usb_hid_report_t;

// Callback function type for HID reports
typedef void (*usb_hid_report_callback_t)(usb_hid_report_t *report);

/**
 * @brief Initialize the USB HID Host
 * 
 * This function initializes the USB HID Host by:
 * 1. Creating an event group for USB host events
 * 2. Creating the USB Host Library Daemon Task
 * 3. Creating the HID Host Client Task
 * 
 * @param report_queue Queue to receive HID reports
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t usb_hid_host_init(QueueHandle_t report_queue);

/**
 * @brief Deinitialize the USB HID Host
 * 
 * This function deinitializes the USB HID Host by:
 * 1. Deleting the HID Host Client Task
 * 2. Deleting the USB Host Library Daemon Task
 * 3. Deleting the event group
 * 4. Resetting all state variables
 * 
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
