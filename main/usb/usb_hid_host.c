#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb_hid_host.h"
#include "hid_bridge.h"
#include <const.h>
#include <task_monitor.h>
#include <vmon.h>
#include <lwip/mem.h>
#include <soc/rtc_cntl_reg.h>
#include "descriptor_parser.h"

#define USB_STATS_INTERVAL_SEC  1
#define DEVICE_EVENT_QUEUE_SIZE 4

static const char *TAG = "USB_HID";
static QueueHandle_t g_device_event_queue = NULL;
static TaskHandle_t g_device_task_handle = NULL;
static usb_hid_report_callback_t g_report_callback = NULL;

typedef struct __attribute__((packed)) {
    hid_host_device_handle_t device_handle;
    hid_host_driver_event_t event;
} usb_device_type_event_t;

static hid_host_device_handle_t g_hid_device_handles[USB_HOST_MAX_INTERFACES] = {NULL};
static bool g_device_connected[USB_HOST_MAX_INTERFACES] = {false};
static TaskHandle_t g_usb_events_task_handle = NULL;
static TaskHandle_t g_stats_task_handle = NULL;
static uint16_t s_current_rps = 0;
static StaticSemaphore_t g_report_maps_mutex_buffer;
static SemaphoreHandle_t g_report_maps_mutex;
static usb_hid_report_t g_report;
static usb_host_client_handle_t client_hdl;
static uint8_t client_addr;
static uint8_t g_num_fields;
static bool usb_host_dev_connected = false;
static usb_hid_field_t *g_fields = NULL;
static int64_t *g_field_values = NULL;
static report_map_t *g_interface_report_maps = NULL;
static report_info_t ***report_lookup_table = NULL;
static uint8_t **g_field_counts = NULL;

static void usb_lib_task(void *arg);
static void usb_stats_task(void *arg);
static void device_event_task(void *arg);
static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, hid_host_driver_event_t event, void *arg);
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, hid_host_interface_event_t event, void *arg);

static void cleanup_all_resources(void) {
    if (g_fields) {
        free(g_fields);
        g_fields = NULL;
    }

    if (g_field_values) {
        free(g_field_values);
        g_field_values = NULL;
    }

    if (g_interface_report_maps) {
        free(g_interface_report_maps);
        g_interface_report_maps = NULL;
    }

    if (g_field_counts) {
        for (int i = 0; i < USB_HOST_MAX_INTERFACES; i++) {
            if (g_field_counts[i]) {
                free(g_field_counts[i]);
            }
        }
        free(g_field_counts);
        g_field_counts = NULL;
    }

    if (report_lookup_table) {
        for (int i = 0; i < USB_HOST_MAX_INTERFACES; i++) {
            if (report_lookup_table[i]) {
                for (int j = 0; j < MAX_REPORTS_PER_INTERFACE; j++) {
                    if (report_lookup_table[i][j]) {
                        free(report_lookup_table[i][j]);
                    }
                }
                free(report_lookup_table[i]);
            }
        }
        free(report_lookup_table);
        report_lookup_table = NULL;
    }

    for (int i = 0; i < USB_HOST_MAX_INTERFACES; i++) {
        if (g_hid_device_handles[i]) {
            hid_host_device_stop(g_hid_device_handles[i]);
            hid_host_device_close(g_hid_device_handles[i]);
            g_hid_device_handles[i] = NULL;
        }
        g_device_connected[i] = false;
    }
}

static void control_transfer_cb(usb_transfer_t *transfer) {
    usb_host_transfer_free(transfer);
}

static void send_linux_like_control_transfers() {
    ESP_LOGI(TAG, "Pretending to be Linux");

    usb_device_handle_t dev_hdl;
    esp_err_t err = usb_host_device_open(client_hdl, client_addr, &dev_hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open device");
        return;
    }

    for (int i = 0; i < 3; i++) {
        usb_transfer_t *transfer;
        err = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 0xFF, 0, &transfer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to allocate transfer: %s", esp_err_to_name(err));
            continue;
        }

        // Setup packet for GET_DESCRIPTOR (Device)
        usb_setup_packet_t *setup = (usb_setup_packet_t *) transfer->data_buffer;
        setup->bmRequestType = 0x80; // Device to Host, Standard, Device
        setup->bRequest = 0x06; // GET_DESCRIPTOR
        setup->wValue = (0x01 << 8); // Device Descriptor
        setup->wIndex = 0;
        setup->wLength = 0xFF; // Set to 0xFF for Linux detection

        transfer->num_bytes = sizeof(usb_setup_packet_t) + 0xFF;
        transfer->device_handle = dev_hdl;
        transfer->bEndpointAddress = 0; // Control endpoint
        transfer->callback = control_transfer_cb;
        transfer->context = NULL;

        err = usb_host_transfer_submit_control(client_hdl, transfer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to submit control transfer: %s", esp_err_to_name(err));
            usb_host_transfer_free(transfer);
        } else {
            vTaskDelay(pdMS_TO_TICKS(25));
        }
    }

    usb_host_client_deregister(client_hdl);
    usb_host_device_close(client_hdl, dev_hdl);
    ESP_LOGI(TAG, "I'm Arch btw");
}

uint8_t usb_hid_host_get_num_fields(const uint8_t report_id, const uint8_t interface_num) {
    if (!g_field_counts || !g_field_counts[interface_num]) {
        return 0;
    }
    return g_field_counts[interface_num][report_id];
}

static void client_event_callback(const usb_host_client_event_msg_t *event_msg, void *);

esp_err_t usb_hid_host_init(const usb_hid_report_callback_t report_callback) {
    ESP_LOGI(TAG, "Initializing USB HID Host");
    if (report_callback == NULL) {
        ESP_LOGE(TAG, "Invalid report callback parameter");
        return ESP_ERR_INVALID_ARG;
    }

    memset(g_hid_device_handles, 0, sizeof(g_hid_device_handles));
    memset(g_device_connected, 0, sizeof(g_device_connected));

    g_interface_report_maps = calloc(USB_HOST_MAX_INTERFACES, sizeof(report_map_t));
    if (!g_interface_report_maps) {
        ESP_LOGE(TAG, "Failed to allocate report maps");
        return ESP_ERR_NO_MEM;
    }

    g_field_counts = calloc(USB_HOST_MAX_INTERFACES, sizeof(uint8_t *));
    if (!g_field_counts) {
        cleanup_all_resources();
        ESP_LOGE(TAG, "Failed to allocate field counts array");
        return ESP_ERR_NO_MEM;
    }

    report_lookup_table = calloc(USB_HOST_MAX_INTERFACES, sizeof(report_info_t **));
    if (!report_lookup_table) {
        cleanup_all_resources();
        ESP_LOGE(TAG, "Failed to allocate lookup table");
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < USB_HOST_MAX_INTERFACES; i++) {
        g_field_counts[i] = calloc(MAX_REPORTS_PER_INTERFACE, sizeof(uint8_t));
        report_lookup_table[i] = calloc(MAX_REPORTS_PER_INTERFACE, sizeof(report_info_t *));
        if (!g_field_counts[i] || !report_lookup_table[i]) {
            cleanup_all_resources();
            ESP_LOGE(TAG, "Failed to allocate interface resources");
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = task_monitor_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init task monitor: %s", esp_err_to_name(err));
    } else {
        task_monitor_start();
    }
    xTaskCreatePinnedToCore(usb_stats_task, "usb_stats", 1500, NULL, 5, &g_stats_task_handle, 1);

    g_report_callback = report_callback;
    g_device_event_queue = xQueueCreate(DEVICE_EVENT_QUEUE_SIZE, sizeof(usb_device_type_event_t));
    if (g_device_event_queue == NULL) {
        cleanup_all_resources();
        ESP_LOGE(TAG, "Failed to create device event queue");
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_created = xTaskCreatePinnedToCore(device_event_task, "dev_evt", 2048, NULL, 6,
                                                      &g_device_task_handle, 1);
    if (task_created != pdTRUE) {
        cleanup_all_resources();
        vQueueDelete(g_device_event_queue);
        return ESP_ERR_NO_MEM;
    }

    g_report_maps_mutex = xSemaphoreCreateMutexStatic(&g_report_maps_mutex_buffer);
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB HID Host: %s", esp_err_to_name(err));
        cleanup_all_resources();
        vTaskDelete(g_device_task_handle);
        vQueueDelete(g_device_event_queue);
        return err;
    }

    task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 1600, NULL, 13, &g_usb_events_task_handle, 1);
    if (task_created != pdTRUE) {
        cleanup_all_resources();
        usb_host_uninstall();
        vTaskDelete(g_device_task_handle);
        vQueueDelete(g_device_event_queue);
        return ESP_ERR_NO_MEM;
    }

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .callback_arg = NULL,
            .client_event_callback = client_event_callback,
        }
    };
    err = usb_host_client_register(&client_config, &client_hdl);
    if (err != ESP_OK) {
        cleanup_all_resources();
        ESP_LOGE(TAG, "Failed to register client: %s", esp_err_to_name(err));
        return err;
    }

    const hid_host_driver_config_t hid_host_config = {
        .create_background_task = true,
        .task_priority = 16,
        .stack_size = 2300,
        .core_id = 1,
        .callback = hid_host_device_callback,
        .callback_arg = NULL
    };
    ESP_ERROR_CHECK(hid_host_install(&hid_host_config));
    ESP_LOGI(TAG, "USB HID Host initialized successfully");
    return ESP_OK;
}

esp_err_t usb_hid_host_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing USB HID Host");
    esp_err_t ret = ESP_OK;

    for (int i = 0; i < USB_HOST_MAX_INTERFACES; i++) {
        if (g_hid_device_handles[i] != NULL && g_device_connected[i]) {
            ESP_LOGD(TAG, "Stopping device on interface %d", i);
            const esp_err_t stop_err = hid_host_device_stop(g_hid_device_handles[i]);
            if (stop_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to stop device on interface %d: %s", i, esp_err_to_name(stop_err));
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    for (int i = 0; i < USB_HOST_MAX_INTERFACES; i++) {
        if (g_hid_device_handles[i] != NULL) {
            ESP_LOGD(TAG, "Closing device on interface %d", i);

            const esp_err_t close_err = hid_host_device_close(g_hid_device_handles[i]);
            if (close_err != ESP_OK) {
                ESP_LOGW(TAG, "Close failed for interface %d: %s", i, esp_err_to_name(close_err));
            }

            g_hid_device_handles[i] = NULL;
            g_device_connected[i] = false;
        }
    }

    g_report_callback = NULL;

    if (client_hdl != NULL) {
        ret = usb_host_client_deregister(client_hdl);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to deregister client: %s", esp_err_to_name(ret));
        }
        client_hdl = NULL;
    }

    if (g_usb_events_task_handle != NULL) {
        vTaskDelete(g_usb_events_task_handle);
        g_usb_events_task_handle = NULL;
    }

    if (g_device_task_handle != NULL) {
        vTaskDelete(g_device_task_handle);
        g_device_task_handle = NULL;
    }

    if (g_device_event_queue != NULL) {
        vQueueDelete(g_device_event_queue);
        g_device_event_queue = NULL;
    }

    if (g_stats_task_handle != NULL) {
        vTaskDelete(g_stats_task_handle);
        g_stats_task_handle = NULL;
    }

    ret = hid_host_uninstall();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to uninstall USB HID Host: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = usb_host_uninstall();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to uninstall USB host: %s", esp_err_to_name(ret));
        return ret;
    }

    cleanup_all_resources();
    ESP_LOGI(TAG, "USB HID Host deinitialized");
    return ret;
}

bool usb_hid_host_device_connected(void) {
    if (is_psu_connected()) {
        return true;
    }

    for (int i = 0; i < USB_HOST_MAX_INTERFACES; i++) {
        if (g_device_connected[i]) {
            return true;
        }
    }
    return false;
}

static uint8_t *data_ptr = NULL;

static IRAM_ATTR void process_report(uint8_t *const data, const size_t length,
                                                                   const uint8_t interface_num) {
    s_current_rps++;
    if (!data || !g_report_callback || length <= 1 || interface_num >= USB_HOST_MAX_INTERFACES) {
        ESP_LOGW(TAG, "Invalid params: data=%p, cb=%p, len=%d, if=%u", data, g_report_callback, length, interface_num);
        return;
    }

    const report_map_t *const report_map = &g_interface_report_maps[interface_num];
    data_ptr = data;
    size_t report_length = length;
    uint8_t report_id = 0;

    if (report_map->num_reports > 1) {
        report_id = data[0];
        data_ptr++;
        report_length--;
    } else if (report_map->num_reports == 1) {
        report_id = report_map->report_ids[0];
    }

    report_info_t *const report_info = report_lookup_table[interface_num][report_id];
    if (!report_info) {
        ESP_LOGW(TAG, "Unknown report ID %d for interface %d", report_id, interface_num);
        return;
    }

    if (g_num_fields != report_info->num_fields) {
        free(g_fields);
        free(g_field_values);
        g_fields = NULL;
        g_field_values = NULL;
    }

    if (!g_fields || !g_field_values) {
        g_fields = calloc(report_info->num_fields, sizeof(usb_hid_field_t));
        g_field_values = calloc(report_info->num_fields, sizeof(int64_t));
        if (!g_fields || !g_field_values) {
            ESP_LOGE(TAG, "Failed to allocate field buffers");
            return;
        }
    }

    g_report.if_id = interface_num;
    g_report.report_id = report_id;
    g_report.type = USB_HID_FIELD_TYPE_INPUT;
    g_report.fields = g_fields;
    g_report.info = report_info;

    const report_field_info_t *const field_info = report_info->fields;
    for (uint8_t i = 0; i < report_info->num_fields; i++) {
        g_field_values[i] = extract_field_value(data_ptr, field_info[i].bit_offset, field_info[i].bit_size);
        g_fields[i].attr = field_info[i].attr;
        g_fields[i].value = &g_field_values[i];
    }

    g_report_callback(&g_report);
}

static uint8_t cur_if_evt_data[64] = {0};

static IRAM_ATTR void hid_host_interface_callback(
    const hid_host_device_handle_t hid_device_handle,
    const hid_host_interface_event_t event, void *arg) {
    static size_t data_length = 0;
    static hid_host_dev_params_t dev_params;
    esp_err_t err;

    err = hid_host_device_get_params(hid_device_handle, &dev_params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device params: %s", esp_err_to_name(err));
        return;
    }

    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            err = hid_host_device_get_raw_input_report_data(hid_device_handle, cur_if_evt_data, sizeof(cur_if_evt_data),
                                                            &data_length);
            if (err != ESP_OK || data_length == 0) {
                ESP_LOGW(TAG, "Failed to get raw input report: %s", esp_err_to_name(err));
                return;
            }
            process_report(cur_if_evt_data, data_length, dev_params.iface_num);
            break;

        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HID Device Disconnected - Interface: %d", dev_params.iface_num);
            hid_host_device_close(hid_device_handle);
            break;

        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            ESP_LOGW(TAG, "HID Device Transfer Error");
            break;

        default:
            ESP_LOGW(TAG, "Unhandled HID Interface Event: %d", event);
            break;
    }
}

static void client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg) {
    ESP_LOGI(TAG, "HID Client Event Received: %d", event_msg->event);

    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        client_addr = event_msg->new_dev.address;
        send_linux_like_control_transfers();
        usb_host_dev_connected = true;
    }
}

static void device_event_task(void *arg) {
    usb_device_type_event_t evt;
    esp_err_t err;

    while (1) {
        if (xQueueReceive(g_device_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            hid_host_dev_params_t dev_params;
            err = hid_host_device_get_params(evt.device_handle, &dev_params);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get device params: %s", esp_err_to_name(err));
                continue;
            }

            if (evt.event == HID_HOST_DRIVER_EVENT_CONNECTED) {
                uint8_t try = 0;
                while (!usb_host_dev_connected && try < 100) {
                    vTaskDelay(pdMS_TO_TICKS(20));
                    try++;
                }

                const hid_host_device_config_t dev_config = {
                    .callback = hid_host_interface_callback,
                    .callback_arg = NULL
                };

                err = hid_host_device_open(evt.device_handle, &dev_config);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
                    continue;
                }
                g_hid_device_handles[dev_params.iface_num] = evt.device_handle;

                err = hid_class_request_set_protocol(evt.device_handle, HID_REPORT_PROTOCOL_REPORT);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to set protocol: %s", esp_err_to_name(err));
                    continue;
                }

                if (HID_PROTOCOL_KEYBOARD == dev_params.proto) {
                    err = hid_class_request_set_idle(evt.device_handle, 0, 0);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to set idle: %s", esp_err_to_name(err));
                        continue;
                    }
                }

                size_t desc_len;
                const uint8_t *desc = hid_host_get_report_descriptor(evt.device_handle, &desc_len);
                if (desc != NULL) {
                    ESP_LOGI(TAG, "Got report descriptor, length = %d", desc_len);
                    if (xSemaphoreTake(g_report_maps_mutex, portMAX_DELAY) == pdTRUE) {
                        parse_report_descriptor(desc, desc_len, dev_params.iface_num,
                                                &g_interface_report_maps[dev_params.iface_num]);
                        report_map_t *report_map = &g_interface_report_maps[dev_params.iface_num];
                        for (int i = 0; i < report_map->num_reports; i++) {
                            ESP_LOGI(TAG, "Expecting %d fields for interface=%d report=%d",
                                     report_map->reports[i].num_fields, dev_params.iface_num,
                                     report_map->report_ids[i]);
                            g_field_counts[dev_params.iface_num][report_map->report_ids[i]] = report_map->reports[i].
                                    num_fields;
                            report_lookup_table[dev_params.iface_num][report_map->report_ids[i]] = &report_map->reports[
                                i];
                        }
                        xSemaphoreGive(g_report_maps_mutex);
                    } else {
                        ESP_LOGE(TAG, "Failed to take report maps mutex");
                    }
                }

                err = hid_host_device_start(evt.device_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start device: %s", esp_err_to_name(err));
                    continue;
                }
                g_device_connected[dev_params.iface_num] = true;
            } else {
                ESP_LOGI(TAG, "Unknown device event, subclass = %d, proto = %s, iface = %d",
                         dev_params.sub_class, dev_params.proto, dev_params.iface_num);
            }
        }
    }
}

static void hid_host_device_callback(const hid_host_device_handle_t hid_device_handle,
                                     const hid_host_driver_event_t event, void *arg) {
    const usb_device_type_event_t evt = {
        .device_handle = hid_device_handle,
        .event = event
    };

    xQueueSend(g_device_event_queue, &evt, 0);
}

static void usb_lib_task(void *arg) {
    ESP_LOGI(TAG, "USB Library task started");
    esp_err_t err;

    while (1) {
        uint32_t event_flags;
        err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "USB host lib handle events failed: %s", esp_err_to_name(err));
            continue;
        }

        if (client_hdl != NULL) {
            usb_host_client_handle_events(client_hdl, 0);
        }

        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients, freeing USB devices");
            err = usb_host_device_free_all();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to free all USB devices: %s", esp_err_to_name(err));
            }
            break;
        }
    }

    ESP_LOGI(TAG, "USB lib task exiting");
    vTaskDelete(NULL);
}

static void usb_stats_task(void *arg) {
    TickType_t last_wake_time = xTaskGetTickCount();

    uint16_t s_prev_rps = 0;
    while (1) {
        const uint16_t reports_per_sec = (s_current_rps - s_prev_rps) / USB_STATS_INTERVAL_SEC;
        if (reports_per_sec > 0) {
            ESP_LOGI(TAG, "USB: %lu rps", reports_per_sec);
        }

        s_prev_rps = s_current_rps;
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(USB_STATS_INTERVAL_SEC * 1000));
    }
}
