#include "hid_bridge.h"

#include <esp_gap_ble_api.h>
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

// ToDo can't increase this because of usb_hid_host
#define HID_QUEUE_SIZE 1
#define HID_QUEUE_ITEM_SIZE sizeof(usb_hid_report_t)

static const char *TAG = "HID_BRIDGE";
static StaticQueue_t s_hid_report_queue_struct;
static uint8_t s_hid_report_queue_storage[HID_QUEUE_SIZE * HID_QUEUE_ITEM_SIZE];
static QueueHandle_t s_hid_report_queue = NULL;
static StaticTimer_t s_inactivity_timer_struct;
static StaticSemaphore_t s_ble_stack_mutex_struct;
static TaskHandle_t s_hid_bridge_task_handle = NULL;
static TimerHandle_t s_inactivity_timer = NULL;
static SemaphoreHandle_t s_ble_stack_mutex = NULL;
static bool s_hid_bridge_initialized = false;
static bool s_hid_bridge_running = false;
static bool s_ble_stack_active = true;
static uint16_t s_sensitivity = 100;

static void hid_bridge_task(void *arg);
static void inactivity_timer_callback(TimerHandle_t xTimer);

static int s_inactivity_timeout_ms = 30 * 1000;
static bool s_enable_sleep = true;
static bool s_verbose = false;

static void inactivity_timer_callback(TimerHandle_t xTimer) {
    if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take BLE stack mutex in inactivity timer");
        return;
    }

    if (!s_ble_stack_active) {
        xSemaphoreGive(s_ble_stack_mutex);
        return;
    }

    if (!usb_hid_host_device_connected() || !ble_hid_device_connected()) {
        xSemaphoreGive(s_ble_stack_mutex);
        return;
    }

    if (is_wifi_connected()) {
        ESP_LOGI(TAG, "Web stack is active, keeping BLE stack running");
        xSemaphoreGive(s_ble_stack_mutex);
        return;
    }

    if (!s_enable_sleep) {
        ESP_LOGI(TAG, "Sleep is disabled in settings, keeping BLE stack running");
        xSemaphoreGive(s_ble_stack_mutex);
        return;
    }

    ESP_LOGI(TAG, "No USB HID events for a while, stopping BLE stack");
    const esp_err_t ret = ble_hid_device_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize BLE HID device: %s", esp_err_to_name(ret));
        s_ble_stack_active = true;
    } else {
        ESP_LOGI(TAG, "BLE stack stopped");
        s_ble_stack_active = false;
        // esp_light_sleep_start();
    }

    xSemaphoreGive(s_ble_stack_mutex);
}

esp_err_t hid_bridge_init(const bool verbose) {
    s_verbose = verbose;
    if (s_hid_bridge_initialized) {
        ESP_LOGW(TAG, "HID bridge already initialized");
        return ESP_OK;
    }

    int sleep_timeout;
    if (storage_get_int_setting("power.sleepTimeout", &sleep_timeout) == ESP_OK) {
        s_inactivity_timeout_ms = sleep_timeout * 1000; // Convert to milliseconds
        ESP_LOGI(TAG, "Sleep timeout set to %d seconds", sleep_timeout);
    } else {
        ESP_LOGW(TAG, "Failed to get sleep timeout from settings, using default");
    }

    bool enable_sleep;
    if (storage_get_bool_setting("power.enableSleep", &enable_sleep) == ESP_OK) {
        s_enable_sleep = enable_sleep;
        ESP_LOGI(TAG, "Sleep %s", enable_sleep ? "enabled" : "disabled");
    } else {
        ESP_LOGW(TAG, "Failed to get enable sleep setting, using default (enabled)");
    }

    s_ble_stack_mutex = xSemaphoreCreateMutexStatic(&s_ble_stack_mutex_struct);
    if (s_ble_stack_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create BLE stack mutex");
        return ESP_ERR_NO_MEM;
    }

    s_ble_stack_active = true;
    s_hid_report_queue = xQueueCreateStatic(HID_QUEUE_SIZE,
        HID_QUEUE_ITEM_SIZE, s_hid_report_queue_storage, &s_hid_report_queue_struct);
    if (s_hid_report_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create HID report queue");
        vSemaphoreDelete(s_ble_stack_mutex);
        s_ble_stack_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_inactivity_timer = xTimerCreateStatic("inactivity_timer", pdMS_TO_TICKS(s_inactivity_timeout_ms),
        pdFALSE, NULL, inactivity_timer_callback, &s_inactivity_timer_struct);
    if (s_inactivity_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create inactivity timer");
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        vSemaphoreDelete(s_ble_stack_mutex);
        s_ble_stack_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = usb_hid_host_init(s_hid_report_queue, verbose);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB HID host: %s", esp_err_to_name(ret));
        xTimerDelete(s_inactivity_timer, 0);
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        return ret;
    }

    ret = ble_hid_device_init(verbose);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE HID device: %s", esp_err_to_name(ret));
        usb_hid_host_deinit();
        xTimerDelete(s_inactivity_timer, 0);
        vQueueDelete(s_hid_report_queue);
        s_hid_report_queue = NULL;
        return ret;
    }

    int mouse_sens;
    if (storage_get_int_setting("mouse.sensitivity", &mouse_sens) == ESP_OK) {
        s_sensitivity = mouse_sens;
        ESP_LOGI(TAG, "Sleep timeout set to %d seconds", sleep_timeout);
    }

    s_hid_bridge_initialized = true;
    ESP_LOGI(TAG, "HID bridge initialized");

    if (xTimerStart(s_inactivity_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start inactivity timer");
    }

    return ESP_OK;
}

esp_err_t hid_bridge_deinit(void) {
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

    if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take BLE stack mutex in deinit");
        return ESP_FAIL;
    }

    if (s_ble_stack_active) {
        s_ble_stack_active = false;
        const esp_err_t ret = ble_hid_device_deinit();
        if (ret != ESP_OK) {
            s_ble_stack_active = true;
            ESP_LOGE(TAG, "Failed to deinitialize BLE HID device: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_ble_stack_mutex);
            return ret;
        }
    }

    const esp_err_t ret = usb_hid_host_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize USB HID host: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_ble_stack_mutex);
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
    xSemaphoreGive(s_ble_stack_mutex);

    return ESP_OK;
}

esp_err_t hid_bridge_start(void) {
    if (!s_hid_bridge_initialized) {
        ESP_LOGE(TAG, "HID bridge not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_hid_bridge_running) {
        ESP_LOGW(TAG, "HID bridge already running");
        return ESP_OK;
    }

    const BaseType_t task_created = xTaskCreatePinnedToCore(hid_bridge_task, "hid_bridge", 2150, NULL, 12,
                                                      &s_hid_bridge_task_handle, 1);
    if (task_created != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create HID bridge task");
        return ESP_ERR_NO_MEM;
    }

    s_hid_bridge_running = true;
    ESP_LOGI(TAG, "HID bridge started");
    return ESP_OK;
}

esp_err_t hid_bridge_stop(void) {
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

static esp_err_t process_keyboard_report(const usb_hid_report_t *report) {
    const uint8_t expected_fields = usb_hid_host_get_num_fields(report->report_id, report->if_id);
    if (expected_fields != report->num_fields) {
        ESP_LOGW(TAG, "Unexpected number of fields: expected=%d, got=%d", expected_fields, report->num_fields);
        return ESP_OK;
    }

    static keyboard_report_t ble_kb_report = {0};
    memset(&ble_kb_report, 0, sizeof(keyboard_report_t));

    uint8_t btn_idx = 0;
    for (int i = 0; i < report->num_fields; i++) {
        const usb_hid_field_t *field = &report->fields[i];
        if (field->value == NULL) {
            continue;
        }

        if (field->attr.usage_page == HID_USAGE_KEYPAD && !field->attr.constant) {
            if (field->attr.usage == HID_KEY_LEFT_CTRL) {
                // field->value is a pointer to the first report array item out of field->attr.report_count
                // for keyboard, field->value[0] will be HID_KEY_LEFT_CTRL
                ble_kb_report.modifier = field->value[0];
                // ESP_LOGI(TAG, "Field[%d]: usg=0x%02x, cnt=%d, sz=%d, value=%d",
                //              i, field->attr.usage, field->attr.report_count, field->attr.report_size, *field->value);
            }
            else if (field->attr.usage == 0 && field->attr.array && !field->attr.constant) {
                memcpy(&ble_kb_report.keycodes[btn_idx], &((uint8_t*)(field->value))[btn_idx], sizeof(uint8_t));
                btn_idx++;

                // ESP_LOGI(TAG, "Field[%d]: usg=0x%02x, cnt=%d, sz=%d, value=[%02X,%02X,%02X,%02X,%02X,%02X]",
                //              i, field->attr.usage, field->attr.report_count, field->attr.report_size,
                //              ble_kb_report.keycodes[0], ble_kb_report.keycodes[1], ble_kb_report.keycodes[2],
                //              ble_kb_report.keycodes[3], ble_kb_report.keycodes[4], ble_kb_report.keycodes[5]);
            } 
        } 
    }

    const esp_err_t ret = ble_hid_device_send_keyboard_report(&ble_kb_report);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send keyboard report: %s", esp_err_to_name(ret));
    }
    return ret;
}

static mouse_report_t ble_mouse_report = {0};

__attribute__((section(".iram1.text"))) static esp_err_t process_mouse_report(const usb_hid_report_t *report)
{
    memset(&ble_mouse_report, 0, sizeof(mouse_report_t));

    const usb_hid_field_t* const btn_field_info = &report->fields[report->info->mouse_fields.buttons];
    ble_mouse_report.buttons = ((uint8_t const*)btn_field_info->value)[0];
    if (report->fields[report->info->mouse_fields.x].attr.report_size == 16) {
        ble_mouse_report.x = ((int16_t const*)report->fields[report->info->mouse_fields.x].value)[0];
        ble_mouse_report.y = ((int16_t const*)report->fields[report->info->mouse_fields.y].value)[0];
    } else if (report->fields[report->info->mouse_fields.x].attr.report_size == 8) {
        ble_mouse_report.x = ((int8_t const*)report->fields[report->info->mouse_fields.x].value)[0];
        ble_mouse_report.y = ((int8_t const*)report->fields[report->info->mouse_fields.y].value)[0];
    }

    ble_mouse_report.wheel = ((uint8_t const*)report->fields[report->info->mouse_fields.wheel].value)[0];
    ble_mouse_report.pan = ((uint8_t const*)report->fields[report->info->mouse_fields.pan].value)[0];

    if (s_sensitivity != 100) {
        ble_mouse_report.x = (int32_t)(int16_t)ble_mouse_report.x * s_sensitivity / 100;
        ble_mouse_report.y = (int32_t)(int16_t)ble_mouse_report.y * s_sensitivity / 100;
    }

    return ble_hid_device_send_mouse_report(&ble_mouse_report);
}

bool hid_bridge_is_ble_paused(void) {
    return !s_ble_stack_active && usb_hid_host_device_connected();
}

esp_err_t hid_bridge_process_report(const usb_hid_report_t *report) {
    if (!s_hid_bridge_initialized) {
        ESP_LOGE(TAG, "HID bridge not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (report == NULL) {
        ESP_LOGE(TAG, "Report is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ble_stack_active) {
        if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(25)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to take BLE stack mutex in process_report");
            return ESP_FAIL;
        }

        if (!s_ble_stack_active) {
            ESP_LOGI(TAG, "USB HID event received, restarting BLE stack");

            const esp_err_t ret = ble_hid_device_init(s_verbose);
            if (ret != ESP_OK) {
                s_ble_stack_active = false;
                ESP_LOGE(TAG, "Failed to initialize BLE HID device: %s", esp_err_to_name(ret));
                xSemaphoreGive(s_ble_stack_mutex);
                return ret;
            }

            s_ble_stack_active = true;
            xSemaphoreGive(s_ble_stack_mutex);
            return ESP_OK;
        }

        xSemaphoreGive(s_ble_stack_mutex);
    }

    if (!ble_hid_device_connected()) {
        ESP_LOGD(TAG, "BLE HID device not connected");
        return ESP_OK;
    }

    esp_err_t ret = ESP_OK;
    if (report->is_keyboard) {
        ret = process_keyboard_report(report);
    } else if (report->is_mouse) {
        ret = process_mouse_report(report);
    }

    if (s_inactivity_timer != NULL && usb_hid_host_device_connected() && ble_hid_device_connected()) {
        xTimerReset(s_inactivity_timer, 0);
    }

    return ret;
}

static void hid_bridge_task(void *arg) {
    ESP_LOGI(TAG, "HID bridge task started");
    usb_hid_report_t report;

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
