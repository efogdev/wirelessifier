#include "hid_bridge.h"
#include <string.h>
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "usb/usb_hid_host.h"
#include "ble_hid_device.h"
#include "web/wifi_manager.h"
#include "utils/storage.h"

static const char *TAG = "HID_BRIDGE";
#define HID_QUEUE_SIZE 4
#define HID_QUEUE_ITEM_SIZE sizeof(usb_hid_report_t)

// Static queue buffer and structure
static StaticQueue_t s_hid_report_queue_struct;
static uint8_t s_hid_report_queue_storage[HID_QUEUE_SIZE * HID_QUEUE_ITEM_SIZE];
static QueueHandle_t s_hid_report_queue = NULL;

// Static timer and mutex structures
static StaticTimer_t s_inactivity_timer_struct;
static StaticSemaphore_t s_ble_stack_mutex_struct;

static TaskHandle_t s_hid_bridge_task_handle = NULL;
static TimerHandle_t s_inactivity_timer = NULL;
static SemaphoreHandle_t s_ble_stack_mutex = NULL;
static bool s_hid_bridge_initialized = false;
static bool s_hid_bridge_running = false;
static bool s_ble_stack_active = true;
static void hid_bridge_task(void *arg);
static void inactivity_timer_callback(TimerHandle_t xTimer);

// Default timeout value, will be updated from settings
static int s_inactivity_timeout_ms = 30 * 1000;
static bool s_enable_sleep = true;

static void inactivity_timer_callback(TimerHandle_t xTimer)
{
    // Take mutex to protect BLE stack state changes
    if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take BLE stack mutex in inactivity timer");
        return;
    }
    
    if (!s_ble_stack_active) {
        xSemaphoreGive(s_ble_stack_mutex);
        return; // BLE stack already stopped
    }
    
    // Only proceed if both USB and BLE devices are connected
    if (!usb_hid_host_device_connected() || !ble_hid_device_connected()) {
        xSemaphoreGive(s_ble_stack_mutex);
        return; // One or both devices not connected
    }
    
    // Don't stop BLE stack if web stack is active (WiFi is connected)
    if (is_wifi_connected()) {
        ESP_LOGI(TAG, "Web stack is active, keeping BLE stack running");
        xSemaphoreGive(s_ble_stack_mutex);
        return;
    }
    
    // Check if sleep is enabled in settings
    if (!s_enable_sleep) {
        ESP_LOGI(TAG, "Sleep is disabled in settings, keeping BLE stack running");
        xSemaphoreGive(s_ble_stack_mutex);
        return;
    }
    
    ESP_LOGI(TAG, "No USB HID events for a while, stopping BLE stack");
    
    // Stop BLE stack
    s_ble_stack_active = false;
    esp_err_t ret = ble_hid_device_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize BLE HID device: %s", esp_err_to_name(ret));
        s_ble_stack_active = true;
    } else {
        ESP_LOGI(TAG, "BLE stack stopped");
        xSemaphoreGive(s_ble_stack_mutex);
        // esp_light_sleep_start();
        return;
    }
    
    xSemaphoreGive(s_ble_stack_mutex);
}

esp_err_t hid_bridge_init(void)
{
    if (s_hid_bridge_initialized) {
        ESP_LOGW(TAG, "HID bridge already initialized");
        return ESP_OK;
    }

    // Get sleep timeout from settings
    int sleep_timeout;
    if (storage_get_int_setting("power.sleepTimeout", &sleep_timeout) == ESP_OK) {
        s_inactivity_timeout_ms = sleep_timeout * 1000; // Convert to milliseconds
        ESP_LOGI(TAG, "Sleep timeout set to %d seconds", sleep_timeout);
    } else {
        ESP_LOGW(TAG, "Failed to get sleep timeout from settings, using default");
    }
    
    // Get enable sleep setting
    bool enable_sleep;
    if (storage_get_bool_setting("power.enableSleep", &enable_sleep) == ESP_OK) {
        s_enable_sleep = enable_sleep;
        ESP_LOGI(TAG, "Sleep %s", enable_sleep ? "enabled" : "disabled");
    } else {
        ESP_LOGW(TAG, "Failed to get enable sleep setting, using default (enabled)");
    }

    // Create BLE stack mutex using static allocation
    s_ble_stack_mutex = xSemaphoreCreateMutexStatic(&s_ble_stack_mutex_struct);
    if (s_ble_stack_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create BLE stack mutex");
        return ESP_ERR_NO_MEM;
    }
    
    s_ble_stack_active = true;

    // Create queue using static allocation
    s_hid_report_queue = xQueueCreateStatic(HID_QUEUE_SIZE,
        HID_QUEUE_ITEM_SIZE, s_hid_report_queue_storage, &s_hid_report_queue_struct);
    if (s_hid_report_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create HID report queue");
        vSemaphoreDelete(s_ble_stack_mutex);
        s_ble_stack_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    // Create inactivity timer using static allocation
    s_inactivity_timer = xTimerCreateStatic(
        "inactivity_timer",
        pdMS_TO_TICKS(s_inactivity_timeout_ms),
        pdFALSE,  // Auto-reload disabled
        NULL,
        inactivity_timer_callback,
        &s_inactivity_timer_struct
    );
    
    if (s_inactivity_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create inactivity timer");
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        vSemaphoreDelete(s_ble_stack_mutex);
        s_ble_stack_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = usb_hid_host_init(s_hid_report_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB HID host: %s", esp_err_to_name(ret));
        xTimerDelete(s_inactivity_timer, 0);
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        return ret;
    }

    ret = ble_hid_device_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE HID device: %s", esp_err_to_name(ret));
        usb_hid_host_deinit();
        xTimerDelete(s_inactivity_timer, 0);
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        return ret;
    }

    s_hid_bridge_initialized = true;
    ESP_LOGI(TAG, "HID bridge initialized");
    
    // Start inactivity timer
    if (xTimerStart(s_inactivity_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start inactivity timer");
    }
    
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

    if (s_inactivity_timer != NULL) {
        xTimerStop(s_inactivity_timer, 0);
        xTimerDelete(s_inactivity_timer, 0);
        s_inactivity_timer = NULL;
    }

    if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take BLE stack mutex in deinit");
        return ESP_FAIL;
    }
    
    if (s_ble_stack_active) {
        s_ble_stack_active = false;
        esp_err_t ret = ble_hid_device_deinit();
        if (ret != ESP_OK) {
            s_ble_stack_active = true;
            ESP_LOGE(TAG, "Failed to deinitialize BLE HID device: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_ble_stack_mutex);
            return ret;
        }
    }
    
    xSemaphoreGive(s_ble_stack_mutex);

    esp_err_t ret = usb_hid_host_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize USB HID host: %s", esp_err_to_name(ret));
        return ret;
    }

    if (s_hid_report_queue != NULL) {
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
    }
    
    if (s_ble_stack_mutex != NULL) {
        vSemaphoreDelete(s_ble_stack_mutex);
        s_ble_stack_mutex = NULL;
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

    BaseType_t task_created = xTaskCreatePinnedToCore(hid_bridge_task, "hid_bridge", 2600, NULL, 14, &s_hid_bridge_task_handle, 1);
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
    static keyboard_report_t ble_kb_report;
    memset(&ble_kb_report, 0, sizeof(ble_kb_report));
    
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
    static mouse_report_t ble_mouse_report;
    memset(&ble_mouse_report, 0, sizeof(ble_mouse_report));
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

bool hid_bridge_is_ble_paused(void)
{
    return !s_ble_stack_active && usb_hid_host_device_connected();
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
    
    // Reset inactivity timer on any USB HID event, but only if both devices are connected
    if (s_inactivity_timer != NULL && usb_hid_host_device_connected() && ble_hid_device_connected()) {
        xTimerReset(s_inactivity_timer, 0);
    }
    
    // If BLE stack is not active, reinitialize it
    if (!s_ble_stack_active) {
        // Take mutex to protect BLE stack state changes
        if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to take BLE stack mutex in process_report");
            return ESP_FAIL;
        }
        
        // Double-check state after taking mutex
        if (!s_ble_stack_active) {
            ESP_LOGI(TAG, "USB HID event received, restarting BLE stack");

            s_ble_stack_active = true;
            esp_err_t ret = ble_hid_device_init();
            if (ret != ESP_OK) {
                s_ble_stack_active = false;
                ESP_LOGE(TAG, "Failed to initialize BLE HID device: %s", esp_err_to_name(ret));
                xSemaphoreGive(s_ble_stack_mutex);
                return ret;
            }
        }
        
        xSemaphoreGive(s_ble_stack_mutex);
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
    
    // Start inactivity timer - it will only take action when both devices are connected
    if (s_inactivity_timer != NULL) {
        if (xTimerStart(s_inactivity_timer, 0) != pdPASS) {
            ESP_LOGE(TAG, "Failed to start inactivity timer");
        }
    }
    
    while (1) {
        if (xQueueReceive(s_hid_report_queue, &report, portMAX_DELAY) == pdTRUE) {
            hid_bridge_process_report(&report);
        }
    }
}
