#include "hid_bridge.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb_hid_host.h"
#include "ble_hid_device.h"

static const char *TAG = "HID_BRIDGE";

// Queue for HID reports
static QueueHandle_t s_hid_report_queue = NULL;

// HID bridge task handle
static TaskHandle_t s_hid_bridge_task_handle = NULL;

// Flag to indicate if HID bridge is initialized
static bool s_hid_bridge_initialized = false;

// Flag to indicate if HID bridge is running
static bool s_hid_bridge_running = false;

// Forward declarations
static void hid_bridge_task(void *arg);

esp_err_t hid_bridge_init(void)
{
    if (s_hid_bridge_initialized) {
        ESP_LOGW(TAG, "HID bridge already initialized");
        return ESP_OK;
    }

    // Create queue for HID reports
    s_hid_report_queue = xQueueCreate(10, sizeof(usb_hid_report_t));
    if (s_hid_report_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create HID report queue");
        return ESP_ERR_NO_MEM;
    }

    // Initialize USB HID host
    esp_err_t ret = usb_hid_host_init(s_hid_report_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB HID host: %s", esp_err_to_name(ret));
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        return ret;
    }

    // Initialize BLE HID device
    ret = ble_hid_device_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE HID device: %s", esp_err_to_name(ret));
        usb_hid_host_deinit();
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        return ret;
    }

    s_hid_bridge_initialized = true;
    ESP_LOGI(TAG, "HID bridge initialized");
    return ESP_OK;
}

esp_err_t hid_bridge_deinit(void)
{
    if (!s_hid_bridge_initialized) {
        ESP_LOGW(TAG, "HID bridge not initialized");
        return ESP_OK;
    }

    // Stop HID bridge if running
    if (s_hid_bridge_running) {
        hid_bridge_stop();
    }

    // Deinitialize BLE HID device
    esp_err_t ret = ble_hid_device_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize BLE HID device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Deinitialize USB HID host
    ret = usb_hid_host_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize USB HID host: %s", esp_err_to_name(ret));
        return ret;
    }

    // Delete queue
    if (s_hid_report_queue != NULL) {
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
    }

    s_hid_bridge_initialized = false;
    ESP_LOGI(TAG, "HID bridge deinitialized");
    return ESP_OK;
}

esp_err_t hid_bridge_start(void)
{
    if (!s_hid_bridge_initialized) {
        ESP_LOGE(TAG, "HID bridge not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_hid_bridge_running) {
        ESP_LOGW(TAG, "HID bridge already running");
        return ESP_OK;
    }

    // Create HID bridge task
    BaseType_t task_created = xTaskCreatePinnedToCore(
        hid_bridge_task,
        "hid_bridge",
        4096,
        NULL,
        5,
        &s_hid_bridge_task_handle,
        0
    );
    if (task_created != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create HID bridge task");
        return ESP_ERR_NO_MEM;
    }

    s_hid_bridge_running = true;
    ESP_LOGI(TAG, "HID bridge started");
    return ESP_OK;
}

esp_err_t hid_bridge_stop(void)
{
    if (!s_hid_bridge_initialized) {
        ESP_LOGE(TAG, "HID bridge not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_hid_bridge_running) {
        ESP_LOGW(TAG, "HID bridge not running");
        return ESP_OK;
    }

    // Delete HID bridge task
    if (s_hid_bridge_task_handle != NULL) {
        vTaskDelete(s_hid_bridge_task_handle);
        s_hid_bridge_task_handle = NULL;
    }

    s_hid_bridge_running = false;
    ESP_LOGI(TAG, "HID bridge stopped");
    return ESP_OK;
}

esp_err_t hid_bridge_process_report(usb_hid_report_t *report)
{
    if (!s_hid_bridge_initialized) {
        ESP_LOGE(TAG, "HID bridge not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (report == NULL) {
        ESP_LOGE(TAG, "Report is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Check if BLE HID device is connected
    if (!ble_hid_device_connected()) {
        ESP_LOGD(TAG, "BLE HID device not connected, starting advertising");
        ble_hid_device_start_advertising();
        return ESP_OK;
    }

    // Process report based on type
    esp_err_t ret = ESP_OK;
    switch (report->type) {
        case USB_HID_REPORT_TYPE_KEYBOARD:
            ESP_LOGD(TAG, "Forwarding keyboard report");
            ret = ble_hid_device_send_keyboard_report(&report->keyboard);
            break;

        case USB_HID_REPORT_TYPE_MOUSE:
            ESP_LOGD(TAG, "Forwarding mouse report");
            ret = ble_hid_device_send_mouse_report(&report->mouse);
            break;

        case USB_HID_REPORT_TYPE_UNKNOWN:
            ESP_LOGW(TAG, "Unknown report type, ignoring");
            break;

        default:
            ESP_LOGW(TAG, "Unhandled report type: %d", report->type);
            break;
    }

    return ret;
}

static void hid_bridge_task(void *arg)
{
    ESP_LOGI(TAG, "HID bridge task started");

    usb_hid_report_t report;
    while (1) {
        // Wait for HID report from USB HID host
        if (xQueueReceive(s_hid_report_queue, &report, portMAX_DELAY) == pdTRUE) {
            // Check if USB HID device is connected
            if (usb_hid_host_device_connected()) {
                // Process report
                hid_bridge_process_report(&report);
            }
        }
    }

    // This point should never be reached
    ESP_LOGE(TAG, "HID bridge task exiting");
    vTaskDelete(NULL);
}
