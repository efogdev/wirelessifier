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

static const char *TAG = "usb_hid_host";

static QueueHandle_t g_report_queue = NULL;
static QueueHandle_t g_event_queue = NULL;
static bool g_device_connected = false;
static TaskHandle_t g_usb_events_task_handle = NULL;
static TaskHandle_t g_event_task_handle = NULL;

#define MAX_REPORT_FIELDS 32
#define MAX_COLLECTION_DEPTH 8

typedef struct {
    usb_hid_field_attr_t attr;
    uint16_t bit_offset;
    uint16_t bit_size;
} report_field_info_t;

typedef struct {
    report_field_info_t fields[MAX_REPORT_FIELDS];
    uint8_t num_fields;
    uint16_t total_bits;
    uint8_t report_id;
} report_map_t;

static report_map_t g_input_report_map = {0};
static uint16_t g_usage_stack[MAX_REPORT_FIELDS];
static uint8_t g_usage_stack_pos = 0;
static uint16_t g_collection_stack[MAX_COLLECTION_DEPTH];
static uint8_t g_collection_depth = 0;

typedef struct {
    enum {
        APP_EVENT_HID_DEVICE,
        APP_EVENT_INTERFACE
    } event_group;
    union {
        struct {
            hid_host_device_handle_t handle;
            hid_host_driver_event_t event;
            void *arg;
        } device_event;
        struct {
            hid_host_device_handle_t handle;
            hid_host_interface_event_t event;
            void *arg;
        } interface_event;
    };
} hid_event_queue_t;

static void usb_lib_task(void *arg);
static void hid_host_event_task(void *arg);
static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, hid_host_driver_event_t event, void *arg);
static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, hid_host_interface_event_t event, void *arg);
static void process_device_event(hid_host_device_handle_t hid_device_handle, hid_host_driver_event_t event, void *arg);
static void process_interface_event(hid_host_device_handle_t hid_device_handle, hid_host_interface_event_t event, void *arg);

esp_err_t usb_hid_host_init(QueueHandle_t report_queue) {
    ESP_LOGI(TAG, "Initializing USB HID Host");
    if (report_queue == NULL) {
        ESP_LOGE(TAG, "Invalid report queue parameter");
        return ESP_ERR_INVALID_ARG;
    }

    g_report_queue = report_queue;
    g_device_connected = false;
    g_event_queue = xQueueCreate(10, sizeof(hid_event_queue_t));
    if (g_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));

    BaseType_t task_created = xTaskCreatePinnedToCore(hid_host_event_task, "hid_events", 4096, NULL, 3, &g_event_task_handle, 0);
    if (task_created != pdTRUE) {
        vQueueDelete(g_event_queue);
        return ESP_ERR_NO_MEM;
    }

    task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096, NULL, 2, &g_usb_events_task_handle, 0);
    if (task_created != pdTRUE) {
        vTaskDelete(g_event_task_handle);
        vQueueDelete(g_event_queue);
        usb_host_uninstall();
        return ESP_ERR_NO_MEM;
    }

    const hid_host_driver_config_t hid_host_config = {
        .create_background_task = true,
        .task_priority = 4,
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

    ret = usb_host_uninstall();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to uninstall USB host: %d", ret);
    }

    if (g_event_queue != NULL) {
        vQueueDelete(g_event_queue);
        g_event_queue = NULL;
    }
    g_report_queue = NULL;
    g_device_connected = false;
    
    ESP_LOGI(TAG, "USB HID Host deinitialized");
    return ret;
}

bool usb_hid_host_device_connected(void) {
    return g_device_connected;
}

static void parse_report_descriptor(const uint8_t *desc, size_t length) {
    uint16_t current_usage_page = 0;
    uint8_t report_size = 0;
    uint8_t report_count = 0;
    int32_t logical_min = 0;
    int32_t logical_max = 0;
    uint16_t current_usage = 0;
    g_input_report_map.num_fields = 0;
    g_input_report_map.total_bits = 0;
    g_usage_stack_pos = 0;
    g_collection_depth = 0;

    for (size_t i = 0; i < length;) {
        uint8_t item = desc[i++];
        uint8_t item_size = item & 0x3;
        uint8_t item_type = (item >> 2) & 0x3;
        uint8_t item_tag = (item >> 4) & 0xF;
        
        uint32_t data = 0;
        if (item_size > 0) {
            for (uint8_t j = 0; j < item_size && i < length; j++) {
                data |= desc[i++] << (j * 8);
            }
        }

        switch (item_type) {
            case 0: // Main
                switch (item_tag) {
                    case 8: // Input
                        if (g_input_report_map.num_fields < MAX_REPORT_FIELDS) {
                            for (uint8_t j = 0; j < report_count; j++) {
                                report_field_info_t *field = &g_input_report_map.fields[g_input_report_map.num_fields];
                                field->attr.usage_page = current_usage_page;
                                // Use the stacked usage if available, otherwise use current_usage
                                if (g_usage_stack_pos > j) {
                                    field->attr.usage = g_usage_stack[j];
                                } else if (g_usage_stack_pos > 0) {
                                    field->attr.usage = g_usage_stack[g_usage_stack_pos - 1];
                                } else {
                                    field->attr.usage = current_usage;
                                }
                                field->attr.report_size = report_size;
                                field->attr.report_count = 1;
                                field->attr.logical_min = logical_min;
                                field->attr.logical_max = logical_max;
                                field->attr.constant = (data & 0x01) != 0;
                                field->attr.variable = (data & 0x02) != 0;
                                field->attr.relative = (data & 0x04) != 0;
                                field->bit_offset = g_input_report_map.total_bits;
                                field->bit_size = report_size;
                                g_input_report_map.total_bits += report_size;
                                g_input_report_map.num_fields++;
                            }
                            g_usage_stack_pos = 0; // Clear usage stack after creating fields
                        }
                        break;
                }
                break;

            case 1: // Global
                switch (item_tag) {
                    case 0: // Usage Page
                        current_usage_page = data;
                        break;
                    case 1: // Logical Minimum
                        logical_min = (int32_t)data;
                        break;
                    case 2: // Logical Maximum
                        logical_max = (int32_t)data;
                        break;
                    case 7: // Report Size
                        report_size = data;
                        break;
                    case 9: // Report Count
                        report_count = data;
                        break;
                }
                break;

            case 2: // Local
                switch (item_tag) {
                    case 0: // Usage
                        if (g_usage_stack_pos < MAX_REPORT_FIELDS) {
                            g_usage_stack[g_usage_stack_pos++] = data;
                        }
                        current_usage = data;
                        break;
                }
                break;
        }
    }

    ESP_LOGI(TAG, "Parsed report descriptor: %d fields, %d total bits", 
             g_input_report_map.num_fields, g_input_report_map.total_bits);
    ESP_LOGI(TAG, "Report descriptor dump:");
    for (size_t i = 0; i < length; i++) {
        printf("%02x ", desc[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");
}

static uint32_t extract_field_value(const uint8_t *data, uint16_t bit_offset, uint16_t bit_size) {
    uint32_t value = 0;
    uint16_t byte_offset = bit_offset / 8;
    uint8_t bit_shift = bit_offset % 8;
    uint16_t bits_remaining = bit_size;
    uint8_t current_byte;

    while (bits_remaining > 0) {
        current_byte = data[byte_offset];
        uint8_t bits_to_read = MIN(8 - bit_shift, bits_remaining);
        uint8_t mask = ((1 << bits_to_read) - 1);
        uint8_t byte_value = (current_byte >> bit_shift) & mask;
        value |= byte_value << (bit_size - bits_remaining);
        
        bits_remaining -= bits_to_read;
        byte_offset++;
        bit_shift = 0;
    }

    return value;
}

static void process_report(hid_host_device_handle_t hid_device_handle, const uint8_t *data, size_t length, uint8_t report_id) {
    // ESP_LOGI(TAG, "Processing report - Handle: %p, Length: %d, Report ID: %d", hid_device_handle, length, report_id);

    if (!data || length == 0 || !g_report_queue) {
        ESP_LOGE(TAG, "Invalid input parameters");
        return;
    }

    usb_hid_report_t report = {
        .report_id = report_id,
        .type = USB_HID_FIELD_TYPE_INPUT,
        .num_fields = g_input_report_map.num_fields,
        .raw_len = MIN(length, sizeof(report.raw))
    };

    static uint32_t field_values[MAX_REPORT_FIELDS];
    static usb_hid_field_t fields[MAX_REPORT_FIELDS];
    
    for (uint8_t i = 0; i < g_input_report_map.num_fields; i++) {
        const report_field_info_t *field_info = &g_input_report_map.fields[i];
        field_values[i] = extract_field_value(data, field_info->bit_offset, field_info->bit_size);
        
        fields[i] = (usb_hid_field_t) {
            .attr = field_info->attr,
            .values = &field_values[i]
        };

        // ESP_LOGI(TAG, "Field %d: usage_page=0x%04x, usage=0x%04x, value=%" PRIu32, 
        //         i, field_info->attr.usage_page, field_info->attr.usage, field_values[i]);
    }

    report.fields = fields;
    memcpy(report.raw, data, report.raw_len);

    BaseType_t queue_result = xQueueSend(g_report_queue, &report, pdMS_TO_TICKS(100));
    if (queue_result != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send report to queue");
    }
}

static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    if (!g_event_queue) {
        return;
    }

    hid_event_queue_t evt = {
        .event_group = APP_EVENT_INTERFACE,
        .interface_event = {
            .handle = hid_device_handle,
            .event = event,
            .arg = arg
        }
    };

    xQueueSend(g_event_queue, &evt, 0);
}

static const char *hid_proto_name_str[] = {
    "NONE",
    "KEYBOARD",
    "MOUSE"
};

static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
    if (!g_event_queue) {
        return;
    }

    hid_event_queue_t evt = {
        .event_group = APP_EVENT_HID_DEVICE,
        .device_event = {
            .handle = hid_device_handle,
            .event = event,
            .arg = arg
        }
    };

    xQueueSend(g_event_queue, &evt, 0);
}

static void process_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
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
        if (desc != NULL && HID_PROTOCOL_KEYBOARD != dev_params.proto && HID_PROTOCOL_NONE != dev_params.proto) {
            ESP_LOGI(TAG, "Got report descriptor, length = %zu", desc_len);
            parse_report_descriptor(desc, desc_len);
        }

        ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));
        g_device_connected = true;
    } else {
        ESP_LOGI(TAG, "Unknown device event, subclass = %d, proto = %s, iface = %d",
            dev_params.sub_class, hid_proto_name_str[dev_params.proto], dev_params.iface_num);
    }
}

static void process_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
    uint8_t data[64] = {0};
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;

    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));
    // ESP_LOGD(TAG, "Interface 0x%x: HID event received", dev_params.iface_num);

    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle, data, sizeof(data), &data_length));
            // ESP_LOGD(TAG, "Raw input report data: length=%d", data_length);
            process_report(hid_device_handle, data, data_length, 0);
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
