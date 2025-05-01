#include "hid_bridge.h"
#include <connection.h>
#include <const.h>
#include <esp_gap_ble_api.h>
#include <hid_device_le_prf.h>
#include <string.h>
#include <vmon.h>
#include <driver/gpio.h>
#include <soc/rtc_cntl_reg.h>
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
#include "buttons.h"
#include "hid_actions.h"
#include "rotary_enc.h"
#include "web/wifi_manager.h"
#include "utils/storage.h"

static const char *TAG = "HID_BRIDGE";
static StaticTimer_t s_inactivity_timer_struct;
static StaticSemaphore_t s_ble_stack_mutex_struct;
static TimerHandle_t s_inactivity_timer = NULL;
static SemaphoreHandle_t s_ble_stack_mutex = NULL;
static bool s_hid_bridge_initialized = false;
static bool s_hid_bridge_running = false;
static bool s_ble_stack_active = true;
static uint16_t s_sensitivity = 100;

static int s_inactivity_timeout_ms = 30 * 1000;
static bool s_enable_sleep = true;

static void inactivity_timer_callback(const TimerHandle_t xTimer) {
    if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to take BLE stack mutex in inactivity timer");
        return;
    }

    if (!usb_hid_host_device_connected() || !ble_hid_device_connected() || !s_ble_stack_active) {
        xTimerReset(xTimer, 0);
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

    if (is_psu_connected()) {
        xTimerReset(xTimer, 0);
        ESP_LOGD(TAG, "Not sleeping while connected to a power source");
        xSemaphoreGive(s_ble_stack_mutex);
        return;
    }

    ESP_LOGI(TAG, "No USB HID events for a while, stopping BLE stack");
    const esp_err_t ret = ble_hid_device_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize BLE HID device: %s", esp_err_to_name(ret));
        s_ble_stack_active = true;
        xSemaphoreGive(s_ble_stack_mutex);
    } else {
        ESP_LOGI(TAG, "BLE stack stopped");
        s_ble_stack_active = false;
        xSemaphoreGive(s_ble_stack_mutex);
    }
}

// ToDo test if it makes any better
// static void sleep_task(void *pvParameters) {
//     while (1) {
//         if (s_ble_stack_active) {
//             vTaskDelay(pdMS_TO_TICKS(1000));
//             continue;
//         }
//
//         // sleep for 250ms
//         esp_sleep_enable_timer_wakeup(250 * 1000); // microseconds
//         esp_light_sleep_start();
//
//         // idle for 1000ms
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }

static void wakeup() {
    if (!s_ble_stack_active) {
        if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(25)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to take BLE stack mutex in process_report");
            return;
        }

        ESP_LOGI(TAG, "Restarting BLE stack…");
        const esp_err_t ret = ble_hid_device_init(VERBOSE);
        if (ret != ESP_OK) {
            s_ble_stack_active = false;
            ESP_LOGE(TAG, "Failed to initialize BLE HID device: %s", esp_err_to_name(ret));
            xSemaphoreGive(s_ble_stack_mutex);
            return;
        }

        s_ble_stack_active = true;
        xSemaphoreGive(s_ble_stack_mutex);

        vTaskDelay(pdMS_TO_TICKS(50));
        if (has_saved_device()) {
            connect_to_saved_device(get_gatts_if());
        }
    }
}

static void rot_cb(const int8_t direction) {
    if (!ble_hid_device_connected()) {
        return;
    }

    char action[24];
    if (direction == 1) {
        if (storage_get_string_setting("buttons.encoder.right", action, sizeof(action)) == ESP_OK) {
            ESP_LOGI(TAG, "Rotate right, action = %s", action);
            execute_action_from_string(ble_conn_id(), "", action, NULL, 0);
        }
    } else if (direction == -1) {
        if (storage_get_string_setting("buttons.encoder.left", action, sizeof(action)) == ESP_OK) {
            ESP_LOGI(TAG, "Rotate left, action = %s", action);
            execute_action_from_string(ble_conn_id(), "", action, NULL, 0);
        }
    }

    xTimerReset(s_inactivity_timer, 0);
    wakeup();
}

static void rot_click_cb(void) {
    if (!ble_hid_device_connected()) {
        return;
    }

    char action[24];
    if (storage_get_string_setting("buttons.encoder.click", action, sizeof(action)) == ESP_OK) {
        ESP_LOGI(TAG, "Click, action = %s", action);
        execute_action_from_string(ble_conn_id(), "", action, NULL, 0);
    }

    xTimerReset(s_inactivity_timer, 0);
    wakeup();
}

static void buttons_cb(const uint8_t button) {
    char keyActionType[32];
    sprintf(keyActionType, "buttons.keys[%d].acType", button);

    char keyAction[32];
    sprintf(keyAction, "buttons.keys[%d].action", button);

    char *mods[4];
    char mod_buffers[4][8];
    for(int i = 0; i < 4; i++) {
        mods[i] = mod_buffers[i];
    }
    char modsPath[32];
    size_t mods_count = 4;
    sprintf(modsPath, "buttons.keys[%d].mods", button);
    storage_get_string_array_setting(modsPath, mods, &mods_count, 8);

    char acType[24], action[24];
    if (storage_get_string_setting(keyActionType, acType, sizeof(acType)) == ESP_OK &&
        storage_get_string_setting(keyAction, action, sizeof(action)) == ESP_OK) {
        ESP_LOGI(TAG, "Click, btn #%d, action type = %s, action = %s", button, acType, action);

        execute_action_from_string(ble_conn_id(), acType, action, (const char**)mods, mods_count);
    }

    xTimerReset(s_inactivity_timer, 0);
    wakeup();
}

esp_err_t hid_bridge_init() {
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
    // xTaskCreatePinnedToCore(sleep_task, "sleep", 1500, NULL, 2, NULL, 1);

    s_inactivity_timer = xTimerCreateStatic("inactivity_timer", pdMS_TO_TICKS(s_inactivity_timeout_ms),
        pdFALSE, NULL, inactivity_timer_callback, &s_inactivity_timer_struct);
    if (s_inactivity_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create inactivity timer");
        vSemaphoreDelete(s_ble_stack_mutex);
        s_ble_stack_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = usb_hid_host_init(hid_bridge_process_report);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize USB HID host: %s", esp_err_to_name(ret));
        xTimerDelete(s_inactivity_timer, 0);
        return ret;
    }

    ret = ble_hid_device_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE HID device: %s", esp_err_to_name(ret));
        usb_hid_host_deinit();
        xTimerDelete(s_inactivity_timer, 0);
        return ret;
    }

    int mouse_sens;
    if (storage_get_int_setting("mouse.sensitivity", &mouse_sens) == ESP_OK) {
        s_sensitivity = mouse_sens;
        ESP_LOGI(TAG, "Sleep timeout set to %d seconds", sleep_timeout);
    }

    s_hid_bridge_initialized = true;
    ESP_LOGI(TAG, "HID bridge initialized");

    if (has_saved_device()) {
        connect_to_saved_device(get_gatts_if());
    }

    if (xTimerStart(s_inactivity_timer, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start inactivity timer");
    }

    rotary_enc_subscribe(rot_cb);
    rotary_enc_subscribe_click(rot_click_cb);
    buttons_subscribe_click(buttons_cb);
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

    s_hid_bridge_running = false;
    ESP_LOGI(TAG, "HID bridge stopped");
    return ESP_OK;
}

static IRAM_ATTR esp_err_t process_keyboard_report(const usb_hid_report_t *report) {
    const uint8_t expected_fields = usb_hid_host_get_num_fields(report->report_id, report->if_id);
    if (expected_fields != report->info->num_fields) {
        ESP_LOGW(TAG, "Unexpected number of fields: expected=%d, got=%d", expected_fields, report->info->num_fields);
        return ESP_OK;
    }

    static keyboard_report_t ble_kb_report = {0};
    memset(&ble_kb_report, 0, sizeof(keyboard_report_t));

    uint8_t btn_idx = 0;
    for (int i = 0; i < report->info->num_fields; i++) {
        const usb_hid_field_t *field = &report->fields[i];
        if (field->value == NULL) {
            continue;
        }

        if (field->attr.usage_page == HID_USAGE_KEYPAD && !field->attr.constant) {
            if (field->attr.usage == HID_KEY_LEFT_CTRL) {
                // field->value is a pointer to the first report array item out of field->attr.report_count
                // for keyboard, field->value[0] will be HID_KEY_LEFT_CTRL
                ble_kb_report.modifier = field->value[0];
            }
            else if (field->attr.usage == 0 && field->attr.array && !field->attr.constant) {
                memcpy(&ble_kb_report.keycodes[btn_idx], &((uint8_t*)(field->value))[btn_idx], sizeof(uint8_t));
                btn_idx++;
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

static IRAM_ATTR esp_err_t process_mouse_report(const usb_hid_report_t *report)
{
    ble_mouse_report.buttons = *report->fields[report->info->mouse_fields.buttons].value;
    ble_mouse_report.x = *report->fields[report->info->mouse_fields.x].value;
    ble_mouse_report.y = *report->fields[report->info->mouse_fields.y].value;
    ble_mouse_report.wheel = *report->fields[report->info->mouse_fields.wheel].value;
    ble_mouse_report.pan = *report->fields[report->info->mouse_fields.pan].value;

    if (s_sensitivity != 100) {
        ble_mouse_report.x = (int32_t)(int16_t)ble_mouse_report.x * s_sensitivity / 100;
        ble_mouse_report.y = (int32_t)(int16_t)ble_mouse_report.y * s_sensitivity / 100;
    }

    return ble_hid_device_send_mouse_report(&ble_mouse_report);
}

bool hid_bridge_is_ble_paused(void) {
    return !s_ble_stack_active && usb_hid_host_device_connected();
}

void IRAM_ATTR hid_bridge_process_report(const usb_hid_report_t *const report) {
    if (!s_hid_bridge_initialized) {
        ESP_LOGE(TAG, "HID bridge not initialized");
        return;
    }

    if (report == NULL) {
        ESP_LOGE(TAG, "Report is NULL");
        return;
    }

    if (!s_ble_stack_active) {
        if (xSemaphoreTake(s_ble_stack_mutex, pdMS_TO_TICKS(25)) != pdTRUE) {
            ESP_LOGW(TAG, "Failed to take BLE stack mutex in process_report");
            return;
        }

        if (!s_ble_stack_active) {
            ESP_LOGI(TAG, "USB HID event received, restarting BLE stack");

            const esp_err_t ret = ble_hid_device_init(VERBOSE);
            if (ret != ESP_OK) {
                s_ble_stack_active = false;
                ESP_LOGE(TAG, "Failed to initialize BLE HID device: %s", esp_err_to_name(ret));
                xSemaphoreGive(s_ble_stack_mutex);
                return;
            }

            s_ble_stack_active = true;
            xSemaphoreGive(s_ble_stack_mutex);

            vTaskDelay(pdMS_TO_TICKS(50));
            if (has_saved_device()) {
                connect_to_saved_device(get_gatts_if());
            }

            return;
        }

        xSemaphoreGive(s_ble_stack_mutex);
        return;
    }

    if (!ble_hid_device_connected()) {
        ESP_LOGD(TAG, "BLE HID device not connected");
        return;
    }

    if (report->info->is_keyboard) {
        process_keyboard_report(report);
    } else if (report->info->is_mouse) {
        process_mouse_report(report);
    }

    if (s_inactivity_timer != NULL && usb_hid_host_device_connected() && ble_hid_device_connected()) {
        xTimerReset(s_inactivity_timer, 0);
    }
}
