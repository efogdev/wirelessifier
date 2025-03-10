/**
 * @file usb_hid_host.c
 * @brief USB HID Host implementation
 * 
 * This file implements the USB HID Host functionality using the ESP-IDF USB Host Library.
 * It follows the architecture described in the USB Host Library documentation, with:
 * - A Host Library Daemon Task (usb_host_task) that handles library events
 * - A Client Task (hid_host_task) that handles client events and HID device communication
 * 
 * The implementation supports HID devices like keyboards and mice, processes their reports,
 * and forwards them to the application through a queue.
 */

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

#define USB_EVENT_DEVICE_CONNECTED BIT0
#define USB_EVENT_DEVICE_DISCONNECTED BIT1
#define USB_EVENT_HOST_INSTALLED BIT2

static EventGroupHandle_t s_usb_event_group;
static QueueHandle_t s_hid_report_queue;
static TaskHandle_t s_usb_host_task_handle;  // USB Host Library Daemon Task
static TaskHandle_t s_hid_host_task_handle;  // HID Host Client Task
static bool s_usb_hid_host_initialized = false;
static bool s_usb_hid_device_connected = false;
static hid_host_device_handle_t s_hid_device_handle = NULL;

static void usb_host_task(void *arg);  // USB Host Library Daemon Task
static void hid_host_task(void *arg);  // HID Host Client Task
static void hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);  // HID device event callback
static void hid_host_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg);  // HID interface event callback

/**
 * @brief Initialize the USB HID Host
 * 
 * This function initializes the USB HID Host by:
 * 1. Creating an event group for USB host events
 * 2. Creating the USB Host Library Daemon Task
 * 3. Creating the HID Host Client Task
 * 
 * @param report_queue Queue to receive HID reports
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
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

    // Create USB host task (Daemon Task)
    // This task handles USB Host Library events that are not specific to any client
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

    // Wait for USB host library to be installed before creating HID host task
    // This ensures proper initialization sequence
    EventBits_t bits = xEventGroupWaitBits(
        s_usb_event_group,
        USB_EVENT_HOST_INSTALLED,
        pdFALSE,  // Don't clear bits after reading
        pdFALSE,  // Wait for any bit, not all bits
        pdMS_TO_TICKS(5000)  // 5 second timeout
    );
    
    if ((bits & USB_EVENT_HOST_INSTALLED) == 0) {
        ESP_LOGE(TAG, "Timeout waiting for USB host library installation");
        vTaskDelete(s_usb_host_task_handle);
        vEventGroupDelete(s_usb_event_group);
        return ESP_ERR_TIMEOUT;
    }

    // Create HID host task (Client Task)
    // This task handles client-specific events and HID device communication
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

/**
 * @brief Deinitialize the USB HID Host
 * 
 * This function deinitializes the USB HID Host by:
 * 1. Deleting the HID Host Client Task
 * 2. Deleting the USB Host Library Daemon Task
 * 3. Deleting the event group
 * 4. Resetting all state variables
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
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

/**
 * @brief USB Host Library Daemon Task
 * 
 * This task is responsible for handling USB Host Library events that are not specific to any client.
 * It follows the pattern described in the USB Host Library documentation:
 * 1. Install the USB Host Library
 * 2. Handle library events in a loop
 * 3. Uninstall the USB Host Library when all clients have deregistered and all devices have been freed
 * 
 * @param arg Task argument (unused)
 */
static void usb_host_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,  
        .intr_flags = ESP_INTR_FLAG_LEVEL1,  
    };

    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB Host library installed successfully");
    
    xEventGroupSetBits(s_usb_event_group, USB_EVENT_HOST_INSTALLED);
    if (arg != NULL) {
        xTaskNotifyGive(arg);
    }


    bool has_clients = true;
    bool has_devices = false;
    while (has_clients) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "No more clients registered");
            
            // Try to free all devices
            if (ESP_OK == usb_host_device_free_all()) {
                ESP_LOGI(TAG, "All devices freed, can uninstall USB Host Library");
                has_clients = false;
            } else {
                ESP_LOGI(TAG, "Waiting for all devices to be freed");
                has_devices = true;
            }
        }
        
        if (has_devices && (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) {
            ESP_LOGI(TAG, "All devices freed");
            has_clients = false;
        }
    }
    
    ESP_LOGI(TAG, "No more clients and devices, uninstalling USB Host Library");
    ESP_ERROR_CHECK(usb_host_uninstall());
    
    ESP_LOGI(TAG, "USB Host task completed");
    vTaskSuspend(NULL);

    ESP_LOGE(TAG, "USB Host task exiting unexpectedly");
    vTaskDelete(NULL);
}

/**
 * @brief HID Host Client Task
 * 
 * This task is responsible for handling HID Host client events and HID device communication.
 * It follows the client task pattern described in the USB Host Library documentation:
 * 1. Register as a client of the USB Host Library
 * 2. Handle client events in a loop
 * 3. Deregister as a client when done
 * 
 * @param arg Task argument (unused)
 */
static void hid_host_task(void *arg)
{
    ESP_LOGI(TAG, "HID Host task started");

    const hid_host_driver_config_t hid_host_config = {
        .create_background_task = false,  // Don't create a background task, we'll handle events in this task
        .task_priority = 5,               // Task priority
        .stack_size = 4096,               // Stack size
        .core_id = 0,                     // Core ID
        .callback = hid_host_device_event, // Device event callback
        .callback_arg = NULL              // Callback argument
    };

    ESP_ERROR_CHECK(hid_host_install(&hid_host_config));

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            s_usb_event_group,
            USB_EVENT_DEVICE_CONNECTED | USB_EVENT_DEVICE_DISCONNECTED,
            pdTRUE,  // Clear bits after reading
            pdFALSE, // Wait for any bit, not all bits
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

    ESP_LOGE(TAG, "HID Host task exiting unexpectedly");
    hid_host_uninstall();  
    vTaskDelete(NULL);
}

/**
 * @brief HID Host device event callback
 * 
 * This callback is called when a HID device event occurs.
 * It handles device connection, opening the device, setting protocols, and starting the device.
 * 
 * @param hid_device_handle Handle to the HID device
 * @param event Device event
 * @param arg Callback argument (unused)
 */
static void hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg)
{
    switch (event) {
        case HID_HOST_DRIVER_EVENT_CONNECTED:
            ESP_LOGI(TAG, "HID Device connected");
            s_hid_device_handle = hid_device_handle;

            // Get device parameters to identify the device type
            hid_host_dev_params_t dev_params;
            ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));
            ESP_LOGI(TAG, "Device: protocol=%s, subclass=%d",
                     dev_params.proto == HID_PROTOCOL_KEYBOARD ? "Keyboard" :
                     dev_params.proto == HID_PROTOCOL_MOUSE ? "Mouse" : "Generic",
                     dev_params.sub_class);

            // Open the device - equivalent to usb_host_device_open() in USB Host Library
            const hid_host_device_config_t dev_config = {
                .callback = hid_host_interface_event,  // Interface event callback
                .callback_arg = NULL                   // Callback argument
            };
            ESP_ERROR_CHECK(hid_host_device_open(hid_device_handle, &dev_config));

            // For boot interface devices (keyboard/mouse), set boot protocol
            if (dev_params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
                ESP_ERROR_CHECK(hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT));
                
                // For keyboards, set idle rate to 0 (no repeat)
                if (dev_params.proto == HID_PROTOCOL_KEYBOARD) {
                    ESP_ERROR_CHECK(hid_class_request_set_idle(hid_device_handle, 0, 0));
                }
            }

            // Start the device - begins receiving reports
            ESP_ERROR_CHECK(hid_host_device_start(hid_device_handle));

            // Signal device connected to the HID Host task
            xEventGroupSetBits(s_usb_event_group, USB_EVENT_DEVICE_CONNECTED);
            break;

        default:
            ESP_LOGW(TAG, "Unhandled HID device event: %d", event);
            break;
    }
}

/**
 * @brief HID Host interface event callback
 * 
 * This callback is called when a HID interface event occurs.
 * It handles input reports, device disconnection, and transfer errors.
 * For input reports, it processes the data and sends it to the application via a queue.
 * 
 * @param hid_device_handle Handle to the HID device
 * @param event Interface event
 * @param arg Callback argument (unused)
 */
static void hid_host_interface_event(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg)
{
    uint8_t data[64];  // Buffer for raw HID report data
    size_t data_length = 0;
    hid_host_dev_params_t dev_params;
    
    // Get device parameters to identify the device type
    ESP_ERROR_CHECK(hid_host_device_get_params(hid_device_handle, &dev_params));

    switch (event) {
        case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
            // Get the raw input report data from the device
            ESP_ERROR_CHECK(hid_host_device_get_raw_input_report_data(hid_device_handle, data, sizeof(data), &data_length));

            // Create and initialize HID report structure
            usb_hid_report_t report;
            memset(&report, 0, sizeof(report));
            report.raw_len = data_length;
            memcpy(report.raw, data, data_length);

            // Process report based on device protocol (keyboard, mouse, or generic)
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

            // Send report to application via queue
            if (s_hid_report_queue != NULL) {
                if (xQueueSend(s_hid_report_queue, &report, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Failed to send report to queue (queue full)");
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
