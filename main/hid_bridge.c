#include "hid_bridge.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "usb/usb_hid_host.h"
#include "ble_hid_device.h"

static const char *TAG = "HID_BRIDGE";
static QueueHandle_t s_hid_report_queue = NULL;
static TaskHandle_t s_hid_bridge_task_handle = NULL;
static bool s_hid_bridge_initialized = false;
static bool s_hid_bridge_running = false;
static void hid_bridge_task(void *arg);

esp_err_t hid_bridge_init(void)
{
    if (s_hid_bridge_initialized) {
        ESP_LOGW(TAG, "HID bridge already initialized");
        return ESP_OK;
    }

    s_hid_report_queue = xQueueCreate(10, sizeof(usb_hid_report_t));
    if (s_hid_report_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create HID report queue");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = usb_hid_host_init(s_hid_report_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB HID host: %s", esp_err_to_name(ret));
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        return ret;
    }

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

    if (s_hid_bridge_running) {
        hid_bridge_stop();
    }

    esp_err_t ret = ble_hid_device_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize BLE HID device: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = usb_hid_host_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize USB HID host: %s", esp_err_to_name(ret));
        return ret;
    }

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

    BaseType_t task_created = xTaskCreatePinnedToCore(hid_bridge_task, "hid_bridge", 8192, NULL, 5, &s_hid_bridge_task_handle, 1);
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

    if (s_hid_bridge_task_handle != NULL) {
        vTaskDelete(s_hid_bridge_task_handle);
        s_hid_bridge_task_handle = NULL;
    }

    s_hid_bridge_running = false;
    ESP_LOGI(TAG, "HID bridge stopped");
    return ESP_OK;
}

static esp_err_t process_keyboard_report(usb_hid_report_t *report) {
    keyboard_report_t ble_kb_report = {0};
    
    // Process each field in the report
    for (int i = 0; i < report->num_fields; i++) {
        const usb_hid_field_t *field = &report->fields[i];
        int value = field->values[0];

        // Handle keyboard fields based on usage page
        if (field->attr.usage_page == HID_USAGE_PAGE_KEYBOARD) {
            if (field->attr.usage >= 0xE0 && field->attr.usage <= 0xE7) {
                // Modifier keys (Ctrl, Shift, Alt, GUI)
                if (value) {
                    ble_kb_report.modifier |= (1 << (field->attr.usage - 0xE0));
                }
            } else if (!field->attr.constant) {
                // Regular keys
                if (value) {
                    // Find first empty slot in keycode array
                    for (int j = 0; j < 6; j++) {
                        if (ble_kb_report.keycode[j] == 0) {
                            ble_kb_report.keycode[j] = field->attr.usage;
                            break;
                        }
                    }
                }
            }
        }
    }

    // ESP_LOGI(TAG, "Forwarding keyboard report: mod=0x%02x key=0x%02x",
    //          ble_kb_report.modifier, ble_kb_report.keycode);

    return ble_hid_device_send_keyboard_report(&ble_kb_report);
}

static esp_err_t process_mouse_report(usb_hid_report_t *report) {
    mouse_report_t ble_mouse_report = {0};
    uint8_t btn_index = 0;

    // Process each field in the report
    for (int i = 0; i < report->num_fields; i++) {
        const usb_hid_field_t *field = &report->fields[i];
        int value = field->values[0];

        // Handle mouse fields based on usage page and usage
        if (field->attr.usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP) {
            switch (field->attr.usage) {
                case HID_USAGE_X:
                    ble_mouse_report.x = value;
                    break;
                case HID_USAGE_Y:
                    ble_mouse_report.y = value;
                    break;
                case HID_USAGE_WHEEL:
                    ble_mouse_report.wheel = value;
                    break;
            }
        } else if (field->attr.usage_page == HID_USAGE_PAGE_BUTTONS) {
            if (field->attr.usage >= 1 && field->attr.usage <= 8 && value) {
                ble_mouse_report.buttons |= (1 << btn_index);
            }
            btn_index++;
        }
    }


    return ble_hid_device_send_mouse_report(&ble_mouse_report);
}

esp_err_t hid_bridge_process_report(usb_hid_report_t *report)
{
    // ESP_LOGD(TAG, "Processing HID report (%d fields)", report->num_fields);

    if (!s_hid_bridge_initialized) {
        ESP_LOGE(TAG, "HID bridge not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (report == NULL) {
        ESP_LOGE(TAG, "Report is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!ble_hid_device_connected()) {
        ESP_LOGD(TAG, "BLE HID device not connected");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    bool is_mouse = false;
    bool is_keyboard = false;

    // Scan all fields to determine report type
    for (int i = 0; i < report->num_fields; i++) {
        const usb_hid_field_t *field = &report->fields[i];
        
        if (field->attr.usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP) {
            switch (field->attr.usage) {
                case HID_USAGE_MOUSE:
                case HID_USAGE_X:
                case HID_USAGE_Y:
                case HID_USAGE_WHEEL:
                    is_mouse = true;
                    break;
                case HID_USAGE_KEYBOARD:
                    is_keyboard = true;
                    break;
            }
        } else if (field->attr.usage_page == HID_USAGE_PAGE_BUTTONS || field->attr.usage_page == HID_USAGE_PAGE_KEYBOARD) {
            is_keyboard = true;
        }
    }

    if (is_mouse) {
        ret = process_mouse_report(report);
    } else if (is_keyboard) {
        ret = process_keyboard_report(report);
    }

    return ret;
}

static void hid_bridge_task(void *arg)
{
    ESP_LOGI(TAG, "HID bridge task started");
    usb_hid_report_t report;
    while (1) {
        if (xQueueReceive(s_hid_report_queue, &report, portMAX_DELAY) == pdTRUE) {
            hid_bridge_process_report(&report);
        }
    }
}
