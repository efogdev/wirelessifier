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
 * @brief Initialize USB HID host
 * 
 * @param report_queue Queue to receive HID reports
 * @return esp_err_t ESP_OK on success
 */
esp_err_t usb_hid_host_init(QueueHandle_t report_queue);

/**
 * @brief Deinitialize USB HID host
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t usb_hid_host_deinit(void);

/**
 * @brief Check if a USB HID device is connected
 * 
 * @return true if connected, false otherwise
 */
bool usb_hid_host_device_connected(void);

#ifdef __cplusplus
}
#endif
