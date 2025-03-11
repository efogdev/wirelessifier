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

#define USB_HID_MAX_RAW_REPORT_SIZE 64

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
#define HID_FEATURE        0xB1

// HID Report Usage Pages from HID Usage Tables 1.12 Section 3, Table 1
#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_PAGE_SIMULATION      0x02
#define HID_USAGE_PAGE_VR              0x03
#define HID_USAGE_PAGE_SPORT           0x04
#define HID_USAGE_PAGE_GAME            0x05
#define HID_USAGE_PAGE_KEY_CODES       0x07
#define HID_USAGE_PAGE_LEDS            0x08
#define HID_USAGE_PAGE_BUTTONS         0x09
#define HID_USAGE_PAGE_KEYBOARD        0x09
#define HID_USAGE_PAGE_CONSUMER        0x0C

// HID Report Usages from HID Usage Tables 1.12 Section 4, Table 6
#define HID_USAGE_POINTER    0x01
#define HID_USAGE_MOUSE      0x02
#define HID_USAGE_JOYSTICK   0x04
#define HID_USAGE_GAMEPAD    0x05
#define HID_USAGE_KEYBOARD   0x06
#define HID_USAGE_KEYPAD     0x07
#define HID_USAGE_X          0x30
#define HID_USAGE_Y          0x31
#define HID_USAGE_Z          0x32
#define HID_USAGE_RX         0x33
#define HID_USAGE_RY         0x34
#define HID_USAGE_RZ         0x35
#define HID_USAGE_SLIDER     0x36
#define HID_USAGE_DIAL       0x37
#define HID_USAGE_WHEEL      0x38
#define HID_USAGE_HAT_SWITCH 0x39

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
