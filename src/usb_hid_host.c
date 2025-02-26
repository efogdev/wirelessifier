#include "usb_hid_host.h"
#include <string.h>
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"
#include "usb/hid_usage_keyboard.h"
#include "usb/hid_usage_mouse.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "USB_HID_HOST";

// Event group for USB host events
static EventGroupHandle_t s_usb_event_group;
#define USB_EVENT_DEVICE_CONNECTED BIT0
#define USB_EVENT_DEVICE_DISCONNECTED BIT1

// Queue for HID reports
static QueueHandle_t s_hid_report_queue;

// USB host task handle
static TaskHandle_t s_usb_host_task_handle;

// HID host driver task handle
static TaskHandle_t s_hid_host_task_handle;

// Flag to indicate if USB HID host is initialized
static bool s_usb_hid_host_initialized = false;

// Flag to indicate if a USB HID device is connected
static bool s_usb_hid_device_connected = false;

// USB HID device handle
static hid_host_device_handle_t s_hid_device_handle = NULL;

// Forward declarations
static void usb_host_task(void *arg);
static void hid_host_task(void *arg);
static void hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);
static void hid_host_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg);

esp_err_t usb_hid_host_init(QueueHandle_t report_queue)
{
    if (s_usb_hid_host_initialized) {
        ESP_LOGW(TAG, "USB HID host already initialized");
        return ESP_OK;
    }

    if (report_queue == NULL) {
        ESP_LOGE(TAG, "Report queue is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_hid_report_queue = report_queue;

    // Create event group for USB host events
    s_usb_event_group = xEventGroupCreate();
    if (s_usb_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Create USB host task
    BaseType_t task_created = xTaskCreatePinnedToCore(
        usb_host_task,
        "usb_host",
        4096,
        NULL,
        5,
        &s_usb_host_task_handle,
        0
    );
    if (task_created != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create USB host task");
        vEventGroupDelete(s_usb_event_group);
        return ESP_ERR_NO_MEM;
    }

    // Create HID host task
    task_created = xTaskCreatePinnedToCore(
        hid_host_task,
        "hid_host",
        4096,
        NULL,
        5,
        &s_hid_host_task_handle,
        0
    );
    if (task_created != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create HID host task");
        vTaskDelete(s_usb_host_task_handle);
        vEventGroupDelete(s_usb_event_group);
        return ESP_ERR_NO_MEM;
    }

    s_usb_hid_host_initialized = true;
    ESP_LOGI(TAG, "USB HID host initialized");
    return ESP_OK;
}

esp_err_t usb_hid_host_deinit(void)
{
    if (!s_usb_hid_host_initialized) {
        ESP_LOGW(TAG, "USB HID host not initialized");
        return ESP_OK;
    }

    // Delete tasks
    vTaskDelete(s_hid_host_task_handle);
    vTaskDelete(s_usb_host_task_handle);

    // Delete event group
    vEventGroupDelete(s_usb_event_group);

    s_usb_hid_host_initialized = false;
    s_usb_hid_device_connected = false;
    s_hid_device_handle = NULL;
    s_hid_report_queue = NULL;

    ESP_LOGI(TAG, "USB HID host deinitialized");
    return ESP_OK;
}

bool usb_hid_host_device_connected(void)
{
    return s_usb_hid_device_connected;
}

static void usb_host_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    ESP_LOGI(TAG, "USB Host task started");

    // Install USB Host driver
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Main USB Host task loop
    while (1) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

        // Handle USB Host events
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No clients, freeing all devices");
            usb_host_device_free_all();
        }
    }

    // This point should never be reached
    ESP_LOGE(TAG, "USB Host task exiting");
    usb_host_uninstall();
    vTaskDelete(NULL);
}

static void hid_host_task(void *arg)
{
    ESP_LOGI(TAG, "HID Host task started");

    // HID host driver configuration
    const hid_host_driver_config_t hid_host_config = {
        .create_background_task = false,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = 0,
        .callback = hid_host_device_event,
        .callback_arg = NULL
    };

    // Install HID host driver
    ESP_ERROR_CHECK(hid_host_install(&hid_host_config));

    // Main HID Host task loop
    while (1) {
        // Wait for USB device connection/disconnection events
        EventBits_t bits = xEventGroupWaitBits(
            s_usb_event_group,
            USB_EVENT_DEVICE_CONNECTED | USB_EVENT_DEVICE_DISCONNECTED,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );

        if (bits & USB_EVENT_DEVICE_CONNECTED) {
            ESP_LOGI(TAG, "USB HID device connected");
            s_usb_hid_device_connected = true;
        }

        if (bits & USB_EVENT_DEVICE_DISCONNECTED) {
            ESP_LOGI(TAG, "USB HID device disconnected");
            s_usb_hid_device_connected = false;
            s_hid_device_handle = NULL;
        }
    }

    // This point should never be reached
    ESP_LOGE(TAG, "HID Host task exiting");
    hid_host_uninstall();
    vTaskDelete(NULL);
}

static void hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg)
{
    switch (event) {
        case HID_HOST_DRIVER_EVENT_CONNECTED:
            ESP_LOGI(TAG, "HID Device connected");
            s_hid_device_handle = hid_device_handle;

            // Get device parameters
            hid_host_dev_params_t dev_params;
            ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));
            ESP_LOGI(TAG, "Device: protocol=%s, subclass=%d",
                     dev_params.proto == HID_PROTOCOL_KEYBOARD ? "Keyboard" :
                     dev_params.proto == HID_PROTOCOL_MOUSE ? "Mouse" : "Generic",
                     dev_params.sub_class);

            // Open the device
            const hid_host_device_config_t dev_config = {
                .callback = hid_host_interface_event,
                .callback_arg = NULL
            };
            ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));

            // Set protocol to boot protocol for keyboard and mouse
            if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));
                if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
                    ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
                }
            }

            // Start the device
            ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));

            // Signal device connected
            xEventGroupSetBits(s_usb_event_group, USB_EVENT_DEVICE_CONNECTED);
            break;

        default:
            break;
    }
}

static void hid_host_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg)
{
    uint8_t data[64];
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            // Get the input report data
            ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle, data, sizeof(data), &data_length));

            // Create HID report
            usb_hid_report_t report;
            memset(&report, 0, sizeof(report));
            report.raw_len = data_length;
            memcpy(report.raw, data, data_length);

            // Set report type based on device protocol
            if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
                report.type = USB_HID_REPORT_TYPE_KEYBOARD;
                if (data_length >= sizeof(usb_hid_keyboard_report_t)) {
                    memcpy(&report.keyboard, data, sizeof(usb_hid_keyboard_report_t));
                }
            } else if (dev_params.proto == HID_PROTOCOL_MOUSE) {
                report.type = USB_HID_REPORT_TYPE_MOUSE;
                if (data_length >= sizeof(usb_hid_mouse_report_t)) {
                    memcpy(&report.mouse, data, sizeof(usb_hid_mouse_report_t));
                }
            } else {
                report.type = USB_HID_REPORT_TYPE_UNKNOWN;
            }

            // Send report to queue
            if (s_hid_report_queue != NULL) {
                if (xQueueSend(s_hid_report_queue, &report, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Failed to send report to queue");
                }
            }
            break;

        case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HID Device disconnected");
            ESP_ERROR_CHECK(hid_host_device_close(hid_device_handle));
            xEventGroupSetBits(s_usb_event_group, USB_EVENT_DEVICE_DISCONNECTED);
            break;

        case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
            ESP_LOGW(TAG, "HID Device transfer error");
            break;

        default:
            ESP_LOGW(TAG, "Unhandled HID event: %d", event);
            break;
    }
}
