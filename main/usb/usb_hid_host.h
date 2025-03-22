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
#include "usb/hid_host.h"
#include "usb/usb_host.h"
#include "descriptor_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

// Map of usage page values to their names
static const char* const usage_page_names[] = {
    [0x01] = "Generic Desktop",
    [0x02] = "Simulation",
    [0x03] = "VR",
    [0x04] = "Sport",
    [0x05] = "Game",
    [0x07] = "Key Codes",
    [0x08] = "LEDs",
    [0x09] = "Buttons",
    [0x0C] = "Consumer"
};

// Map of usage values to their names (Generic Desktop page)
static const char* const usage_names_gendesk[] = {
    [0x01] = "Pointer",
    [0x02] = "Mouse",
    [0x04] = "Joystick",
    [0x05] = "Gamepad",
    [0x06] = "Keyboard",
    [0x07] = "Keypad",
    [0x30] = "X",
    [0x31] = "Y",
    [0x32] = "Z",
    [0x33] = "Rx",
    [0x34] = "Ry",
    [0x35] = "Rz",
    [0x36] = "Slider",
    [0x37] = "Dial",
    [0x38] = "Wheel",
    [0x39] = "Hat Switch"
};

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
