/**
 * @file usb_hid_host.c
 * @brief USB HID Host implementation with full report descriptor support
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb_hid_host.h"

static const char *TAG = "usb_hid_host";

// Global variables
static QueueHandle_t g_report_queue = NULL;
static bool g_device_connected = false;
static TaskHandle_t g_usb_events_task_handle = NULL;

// Forward declarations
static void usb_lib_task(void *arg);
static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                   const hid_host_driver_event_t event,
                                   void *arg);
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                      const hid_host_interface_event_t event,
                                      void *arg);

esp_err_t usb_hid_host_init(QueueHandle_t report_queue)
{
    ESP_LOGI(TAG, "Initializing USB HID Host");
    if (report_queue == NULL) {
        ESP_LOGE(TAG, "Invalid report queue parameter");
        return ESP_ERR_INVALID_ARG;
    }

    g_report_queue = report_queue;
    g_device_connected = false;

    // Initialize USB Host Library
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Create USB Events Task
    BaseType_t task_created = xTaskCreatePinnedToCore(usb_lib_task,
                                                     "usb_events",
                                                     4096,
                                                     NULL,
                                                     2,
                                                     &g_usb_events_task_handle,
                                                     0);
    if (task_created != pdTRUE) {
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    // Initialize HID Host Driver
    const hid_host_driver_config_t hid_host_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_callback,
        .callback_arg = NULL
    };

    esp_err_t ret = hid_host_install(&hid_host_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install HID host driver: %d", ret);
        vTaskDelete(g_usb_events_task_handle);
        usb_host_uninstall();
        return ret;
    }
    ESP_LOGI(TAG, "USB HID Host initialized successfully");

    return ESP_OK;
}

esp_err_t usb_hid_host_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing USB HID Host");
    esp_err_t ret = hid_host_uninstall();
    if (ret != ESP_OK) {
        return ret;
    }

    // Signal USB events task to exit
    if (g_usb_events_task_handle != NULL) {
        vTaskDelete(g_usb_events_task_handle);
        g_usb_events_task_handle = NULL;
    }

    ret = usb_host_uninstall();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to uninstall USB host: %d", ret);
    }
    g_report_queue = NULL;
    g_device_connected = false;
    ESP_LOGI(TAG, "USB HID Host deinitialized");
    return ret;
}

bool usb_hid_host_device_connected(void)
{
    return g_device_connected;
}

static void process_report(const uint8_t *data, size_t length, uint8_t report_id)
{
    if (!data || length == 0 || !g_report_queue) {
        return;
    }

    usb_hid_report_t report = {
        .report_id = report_id,
        .type = USB_HID_FIELD_TYPE_INPUT,
        .num_fields = 0,
        .fields = NULL,
        .raw_len = MIN(length, sizeof(report.raw))
    };

    // Copy raw data
    memcpy(report.raw, data, report.raw_len);

    // Send report to queue
    xQueueSend(g_report_queue, &report, 0);
}

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle,
                                      const hid_host_interface_event_t event,
                                      void *arg)
{
    uint8_t data[64] = { 0 };
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
        ESP_LOGD(TAG, "Received input report from interface 0x%x", dev_params.iface_num);
        ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle,
                                                                 data,
                                                                 sizeof(data),
                                                                 &data_length));
        ESP_LOGD(TAG, "Raw input report data: length=%d", data_length);
        
        // Process the raw report data
        process_report(data, data_length, 0);
        break;

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HID Device Disconnected - Interface: %d", dev_params.iface_num);
        g_device_connected = false;
        ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
        break;

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "HID Device Transfer Error");
        break;

    default:
        ESP_LOGW(TAG, "Unhandled HID Interface Event: %d", event);
        break;
    }
}

static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle,
                                   const hid_host_driver_event_t event,
                                   void *arg)
{
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "HID Device Connected");
        
        const hid_host_device_config_t dev_config = {
            .callback = hid_host_interface_callback,
            .callback_arg = NULL
        };

        ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
        

        // Always use report protocol
        ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, 
                                                     HID_REPORT_PROTOCOL_REPORT));
        
        ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        g_device_connected = true;
    }
}

static void usb_lib_task(void *arg)
{
    ESP_LOGI(TAG, "USB Library task started");
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        // Check for no clients event
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients, freeing USB devices");
            ESP_ERROR_CHECK(usb_host_device_free_all());
            break;
        }
    }

    vTaskDelete(NULL);
}
