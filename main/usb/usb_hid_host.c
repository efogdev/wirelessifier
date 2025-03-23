#pragma pack(push, 1)
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

#define USB_STATS_INTERVAL_SEC  2
#define HOST_HID_QUEUE_SIZE     1
#define DEVICE_EVENT_QUEUE_SIZE 6

static const char *TAG = "usb_hid_host";
static QueueHandle_t g_report_queue = NULL;
static QueueHandle_t g_device_event_queue = NULL;
static TaskHandle_t g_device_task_handle = NULL;

typedef struct {
    hid_host_device_handle_t device_handle;
    hid_host_driver_event_t event;
} usb_device_type_event_t;

static bool g_device_connected = false;
static TaskHandle_t g_usb_events_task_handle = NULL;
static TaskHandle_t g_stats_task_handle = NULL;
static uint32_t s_current_rps = 0;
static StaticSemaphore_t g_report_maps_mutex_buffer;
static SemaphoreHandle_t g_report_maps_mutex;
static usb_hid_report_t g_report;
static usb_hid_field_t g_fields[MAX_REPORT_FIELDS];
static int g_field_values[MAX_REPORT_FIELDS];
static report_map_t g_interface_report_maps[USB_HOST_MAX_INTERFACES] = {0};
static bool g_verbose = false;
static uint8_t g_field_counts[USB_HOST_MAX_INTERFACES][MAX_REPORTS_PER_INTERFACE] = {0};

static void usb_lib_task(void *arg);
static void usb_stats_task(void *arg);
static void device_event_task(void *arg);
static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, hid_host_driver_event_t event, void *arg);
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, hid_host_interface_event_t event, void *arg);

uint8_t usb_hid_host_get_num_fields(const uint8_t report_id, const uint8_t interface_num) {
    return g_field_counts[interface_num][report_id];
}

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
    g_device_event_queue = xQueueCreate(DEVICE_EVENT_QUEUE_SIZE, sizeof(usb_device_type_event_t));
    if (g_device_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create device event queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(device_event_task, "dev_evt", 2048, NULL, 11,
                                                      &g_device_task_handle, 1);
    if (task_created != pdTRUE) {
        vQueueDelete(g_device_event_queue);
        return ESP_ERR_NO_MEM;
    }

    g_report_maps_mutex = xSemaphoreCreateMutexStatic(&g_report_maps_mutex_buffer);
    g_device_connected = false;

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));
    task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 2600, NULL, 12, &g_usb_events_task_handle, 1);
    if (task_created != pdTRUE) {
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

    g_report.if_id = interface_num;
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

    g_report.is_keyboard = report_info->is_keyboard;
    g_report.is_mouse = report_info->is_mouse;

    memcpy(g_report.raw, report_data, g_report.raw_len);
    xQueueSend(g_report_queue, &g_report, 0);
}

static uint8_t cur_if_evt_data[64] = {0};

static void hid_host_interface_callback(const hid_host_device_handle_t hid_device_handle,
                                        const hid_host_interface_event_t event, void *arg) {
    static size_t data_length = 0;
    static hid_host_dev_params_t dev_params;

    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));
    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            ESP_ERROR_CHECK(
                hid_host_device_get_raw_input_report_data(hid_device_handle, cur_if_evt_data, sizeof(cur_if_evt_data), &
                    data_length));
            process_report(cur_if_evt_data, data_length, dev_params.iface_num);
            break;

        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HID Device Disconnected - Interface: %d", dev_params.iface_num);
            g_device_connected = false;
            memset(g_field_counts[dev_params.iface_num], 0, sizeof(g_field_counts[dev_params.iface_num]));
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

static void device_event_task(void *arg) {
    usb_device_type_event_t evt;

    while (1) {
        if (xQueueReceive(g_device_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            hid_host_dev_params_t dev_params;
            ESP_ERROR_CHECK(hid_host_device_get_params(evt.device_handle, &dev_params));

            if (evt.event == HID_HOST_DRIVER_EVENT_CONNECTED) {
                const hid_host_device_config_t dev_config = {
                    .callback = hid_host_interface_callback,
                    .callback_arg = NULL
                };

                ESP_ERROR_CHECK(hid_host_device_open(evt.device_handle, &dev_config));
                ESP_ERROR_CHECK(hid_class_request_set_protocol(evt.device_handle, HID_REPORT_PROTOCOL_REPORT));
                if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
                    ESP_ERROR_CHECK(hid_class_request_set_idle(evt.device_handle, 0, 0));
                }

                size_t desc_len;
                const uint8_t *desc = hid_host_get_report_descriptor(evt.device_handle, &desc_len);
                if (desc != NULL) {
                    ESP_LOGI(TAG, "Got report descriptor, length = %d", desc_len);
                    if (xSemaphoreTake(g_report_maps_mutex, portMAX_DELAY) == pdTRUE) {
                        parse_report_descriptor(desc, desc_len, dev_params.iface_num,
                                                &g_interface_report_maps[dev_params.iface_num]);
                        const report_map_t *report_map = &g_interface_report_maps[dev_params.iface_num];
                        for (int i = 0; i < report_map->num_reports; i++) {
                            ESP_LOGI(TAG, "Expecting %d fields for interface=%d report=%d",
                                     report_map->reports[i].num_fields, dev_params.iface_num,
                                     report_map->report_ids[i]);
                            g_field_counts[dev_params.iface_num][report_map->report_ids[i]] = report_map->reports[i].
                                    num_fields;
                        }
                        xSemaphoreGive(g_report_maps_mutex);
                    } else {
                        ESP_LOGE(TAG, "Failed to take report maps mutex");
                    }
                }

                ESP_ERROR_CHECK(hid_host_device_start(evt.device_handle));
                g_device_connected = true;
            } else {
                ESP_LOGI(TAG, "Unknown device event, subclass = %d, proto = %s, iface = %d",
                         dev_params.sub_class, dev_params.proto, dev_params.iface_num);
            }
        }
    }
}

static void hid_host_device_callback(const hid_host_device_handle_t hid_device_handle,
                                     const hid_host_driver_event_t event, void *arg) {
    usb_device_type_event_t evt = {
        .device_handle = hid_device_handle,
        .event = event
    };

    xQueueSend(g_device_event_queue, &evt, 0);
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
    if (g_device_task_handle) {
        vTaskDelete(g_device_task_handle);
        g_device_task_handle = NULL;
    }
    if (g_device_event_queue) {
        vQueueDelete(g_device_event_queue);
        g_device_event_queue = NULL;
    }
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

#pragma pack(pop)
