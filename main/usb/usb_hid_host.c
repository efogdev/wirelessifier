/**
 * @file usb_hid_host.c
 * @brief USB HID Host implementation with full report descriptor support
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb_hid_host.h"
#include <task_monitor.h>
#include "descriptor_parser.h"

#define USB_STATS_INTERVAL_SEC 2
#define HOST_HID_QUEUE_SIZE    4

static const char *TAG = "usb_hid_host";
static QueueHandle_t g_report_queue = NULL;
static StaticQueue_t g_event_queue_storage;
static QueueHandle_t g_event_queue = NULL;
static uint8_t g_event_queue_array[HOST_HID_QUEUE_SIZE * sizeof(hid_event_queue_t)];
static bool g_device_connected = false;
static TaskHandle_t g_usb_events_task_handle = NULL;
static TaskHandle_t g_event_task_handle = NULL;
static TaskHandle_t g_stats_task_handle = NULL;
static uint32_t s_current_rps = 0;
static StaticSemaphore_t g_report_maps_mutex_buffer;
static SemaphoreHandle_t g_report_maps_mutex;
static usb_hid_report_t g_report;
static usb_hid_field_t g_fields[MAX_REPORT_FIELDS];
static int g_field_values[MAX_REPORT_FIELDS];
static report_map_t g_interface_report_maps[USB_HOST_MAX_INTERFACES] = {0};
static bool g_verbose = false;

static void usb_lib_task(void *arg);
static void hid_host_event_task(void *arg);
static void usb_stats_task(void *arg);
static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, hid_host_driver_event_t event, void *arg);
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, hid_host_interface_event_t event, void *arg);
static void process_device_event(hid_host_device_handle_t hid_device_handle, hid_host_driver_event_t event, void *arg);
static void process_interface_event(hid_host_device_handle_t hid_device_handle, hid_host_interface_event_t event, void *arg);

esp_err_t usb_hid_host_init(const QueueHandle_t report_queue, const bool verbose) {
    ESP_LOGI(TAG, "Initializing USB HID Host");
    if (report_queue == NULL) {
        ESP_LOGE(TAG, "Invalid report queue parameter");
        return ESP_ERR_INVALID_ARG;
    }

    if (verbose) {
        ESP_ERROR_CHECK(task_monitor_init());
        ESP_ERROR_CHECK(task_monitor_start());

        xTaskCreatePinnedToCore(usb_stats_task, "usb_stats", 2048, NULL, 5, &g_stats_task_handle, 1);
    }

    g_verbose = verbose;
    g_report_queue = report_queue;
    g_report_maps_mutex = xSemaphoreCreateMutexStatic(&g_report_maps_mutex_buffer);

    g_device_connected = false;
    g_event_queue = xQueueCreateStatic(HOST_HID_QUEUE_SIZE, sizeof(hid_event_queue_t), g_event_queue_array, &g_event_queue_storage);

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));

    BaseType_t task_created = xTaskCreatePinnedToCore(hid_host_event_task, "hid_events", 3400, NULL, 13, &g_event_task_handle, 1);
    if (task_created != pdTRUE) {
        vQueueDelete(g_event_queue);
        return ESP_ERR_NO_MEM;
    }

    task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 2600, NULL, 12, &g_usb_events_task_handle, 1);
    if (task_created != pdTRUE) {
        vTaskDelete(g_event_task_handle);
        vQueueDelete(g_event_queue);
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    const hid_host_driver_config_t hid_host_config = {
        .create_background_task = true,
        .task_priority = 12,
        .stack_size = 2800,
        .core_id = 1,
        .callback = hid_host_device_callback,
        .callback_arg = NULL
    };

    const esp_err_t ret = hid_host_install(&hid_host_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install HID host driver: %d", ret);
        vTaskDelete(g_usb_events_task_handle);
        usb_host_uninstall();
        return ret;
    }

    ESP_LOGI(TAG, "USB HID Host initialized successfully");
    return ESP_OK;
}

esp_err_t usb_hid_host_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing USB HID Host");
    esp_err_t ret = hid_host_uninstall();
    if (ret != ESP_OK) {
        return ret;
    }

    if (g_event_task_handle != NULL) {
        vTaskDelete(g_event_task_handle);
        g_event_task_handle = NULL;
    }
    if (g_usb_events_task_handle != NULL) {
        vTaskDelete(g_usb_events_task_handle);
        g_usb_events_task_handle = NULL;
    }

    if (g_stats_task_handle != NULL) {
        vTaskDelete(g_stats_task_handle);
        g_stats_task_handle = NULL;
    }

    ret = usb_host_uninstall();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to uninstall USB host: %d", ret);
    }

    g_event_queue = NULL;
    g_report_queue = NULL;
    g_device_connected = false;
    ESP_LOGI(TAG, "USB HID Host deinitialized");
    return ret;
}

bool usb_hid_host_device_connected(void) {
    return g_device_connected;
}

static void process_report(const uint8_t *const data, const size_t length, const uint8_t interface_num) {
    s_current_rps++;
    if (!data || !g_report_queue || length <= 1 || interface_num >= USB_HOST_MAX_INTERFACES) {
        ESP_LOGE(TAG, "Invalid parameters: data=%p, queue=%p, len=%d, iface=%u", data, g_report_queue, length, interface_num);
        return;
    }

    // char hex_str[length * 3 + 1];
    // hex_str[0] = '\0';
    // for (size_t i = 0; i < length; i++) {
    //     sprintf(hex_str + i * 3, "%02X ", data[i]);
    // }

    const report_map_t *const report_map = &g_interface_report_maps[interface_num];
    const uint8_t *report_data = data;
    size_t report_length = length;
    static uint8_t report_id = 0;
    if (report_map->num_reports > 1) {
        report_id = data[0];
        report_data++;
        report_length--;
    } else if (report_map->num_reports == 1) {
        report_id = report_map->report_ids[0];
    }

    // ESP_LOGI(TAG, "HID [%d] %s", report_id, hex_str);
    
    const report_info_t *report_info = NULL;
    for (int i = 0; i < report_map->num_reports; i++) {
        if (report_map->report_ids[i] == report_id) {
            report_info = &report_map->reports[i];
            break;
        }
    }
    
    if (!report_info) {
        ESP_LOGW(TAG, "Unknown report ID %d for interface %d", report_id, interface_num);
        return;
    }

    g_report.report_id = report_id;
    g_report.type = USB_HID_FIELD_TYPE_INPUT;
    g_report.num_fields = report_info->num_fields;
    g_report.raw_len = MIN(report_length, sizeof(g_report.raw));
    g_report.fields = g_fields;

    const report_field_info_t *const field_info = report_info->fields;
    for (uint8_t i = 0; i < report_info->num_fields; i++) {
        g_field_values[i] = extract_field_value(report_data, field_info[i].bit_offset, field_info[i].bit_size);
        g_fields[i].attr = field_info[i].attr;
        g_fields[i].values = &g_field_values[i];
    }

    memcpy(g_report.raw, report_data, g_report.raw_len);
    xQueueSend(g_report_queue, &g_report, 0);
}

static hid_event_queue_t g_interface_event = {
    .event_group = APP_EVENT_INTERFACE
};

static void hid_host_interface_callback(const hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    if (!g_event_queue) {
        return;
    }

    g_interface_event.interface_event = (interface_event_t){
        .handle = hid_device_handle,
        .event = event,
        .arg = arg
    };

    xQueueSend(g_event_queue, &g_interface_event, 0);
}

static hid_event_queue_t g_device_event = {
    .event_group = APP_EVENT_HID_DEVICE
};

static void hid_host_device_callback(const hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
    if (!g_event_queue) {
        return;
    }

    g_device_event.device_event = (device_event_t){
        .handle = hid_device_handle,
        .event = event,
        .arg = arg
    };

    xQueueSend(g_event_queue, &g_device_event, 0);
}

static const char *hid_proto_name_str[] = {
    "UNKNOWN",
    "KEYBOARD",
    "MOUSE"
};

static void process_device_event(const hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    if (event == HID_HOST_DRIVER_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "HID Device Connected, proto = %s, subclass = %d", hid_proto_name_str[dev_params.proto], dev_params.sub_class);

        const hid_host_device_config_t dev_config = {
            .callback = hid_host_interface_callback,
            .callback_arg = NULL
        };

        ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));
        ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_REPORT));
        if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
            ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
        }

        // Get and parse report descriptor
        size_t desc_len;
        const uint8_t *desc = hid_host_get_report_descriptor(hid_device_handle, &desc_len);
        if (desc != NULL) {
            ESP_LOGI(TAG, "Got report descriptor, length = %d", desc_len);
            if (xSemaphoreTake(g_report_maps_mutex, portMAX_DELAY) == pdTRUE) {
                parse_report_descriptor(desc, desc_len, dev_params.iface_num, &g_interface_report_maps[dev_params.iface_num]);
                xSemaphoreGive(g_report_maps_mutex);
            } else {
                ESP_LOGE(TAG, "Failed to take report maps mutex");
            }
        }

        ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        g_device_connected = true;
    } else {
        ESP_LOGI(TAG, "Unknown device event, subclass = %d, proto = %s, iface = %d",
            dev_params.sub_class, hid_proto_name_str[dev_params.proto], dev_params.iface_num);
    }
}

static uint8_t cur_if_evt_data[128] = {0};

static void process_interface_event(const hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    static size_t data_length = 0;
    static hid_host_dev_params_t dev_params;

    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle, cur_if_evt_data, sizeof(cur_if_evt_data), &data_length));
            process_report(cur_if_evt_data, data_length, dev_params.iface_num);
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

static void hid_host_event_task(void *arg) {
    hid_event_queue_t evt;

    while (1) {
        if (g_event_queue == NULL) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (xQueueReceive(g_event_queue, &evt, portMAX_DELAY)) {
            switch (evt.event_group) {
                case APP_EVENT_HID_DEVICE:
                    process_device_event(evt.device_event.handle, evt.device_event.event, evt.device_event.arg);
                    break;
                case APP_EVENT_INTERFACE:
                    process_interface_event(evt.interface_event.handle, evt.interface_event.event, evt.interface_event.arg);
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown event group: %d", evt.event_group);
                    break;
            }
        }
    }
}

static void usb_lib_task(void *arg) {
    ESP_LOGI(TAG, "USB Library task started");
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients, freeing USB devices");
            ESP_ERROR_CHECK(usb_host_device_free_all());
            break;
        }
    }

    ESP_LOGI(TAG, "USB shutdown");
    vTaskDelay(10);
    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskDelete(NULL);
}

static void usb_stats_task(void *arg) {
    uint32_t prev_count = 0;
    TickType_t last_wake_time = xTaskGetTickCount();

    while (1) {
        const uint32_t reports_per_sec = (s_current_rps - prev_count) / USB_STATS_INTERVAL_SEC;
        ESP_LOGI(TAG, "USB: %lu rps", reports_per_sec);
        
        prev_count = s_current_rps;
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(USB_STATS_INTERVAL_SEC * 1000));
    }
}
