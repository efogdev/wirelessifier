#include "ble_hid_device.h"
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "BLE_HID_DEVICE";

// Flag to indicate if BLE HID device is initialized
static bool s_ble_hid_device_initialized = false;

// Flag to indicate if BLE HID device is connected
static bool s_ble_hid_device_connected = false;

// HID device handle
static esp_hidd_dev_t *s_hid_dev = NULL;

// BLE advertising parameters
static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x30,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// HID report map for keyboard and mouse
static const uint8_t hid_report_map[] = {
    // Keyboard report descriptor (8 bytes)
    0x05, 0x01,                     // Usage Page (Generic Desktop)
    0x09, 0x06,                     // Usage (Keyboard)
    0xA1, 0x01,                     // Collection (Application)
    0x85, 0x01,                     //   Report ID (1)
    0x05, 0x07,                     //   Usage Page (Key Codes)
    0x19, 0xE0,                     //   Usage Minimum (224)
    0x29, 0xE7,                     //   Usage Maximum (231)
    0x15, 0x00,                     //   Logical Minimum (0)
    0x25, 0x01,                     //   Logical Maximum (1)
    0x75, 0x01,                     //   Report Size (1)
    0x95, 0x08,                     //   Report Count (8)
    0x81, 0x02,                     //   Input (Data, Variable, Absolute)
    0x95, 0x01,                     //   Report Count (1)
    0x75, 0x08,                     //   Report Size (8)
    0x81, 0x01,                     //   Input (Constant)
    0x95, 0x06,                     //   Report Count (6)
    0x75, 0x08,                     //   Report Size (8)
    0x15, 0x00,                     //   Logical Minimum (0)
    0x25, 0x65,                     //   Logical Maximum (101)
    0x05, 0x07,                     //   Usage Page (Key Codes)
    0x19, 0x00,                     //   Usage Minimum (0)
    0x29, 0x65,                     //   Usage Maximum (101)
    0x81, 0x00,                     //   Input (Data, Array)
    0xC0,                           // End Collection

    // Mouse report descriptor (8 bytes)
    0x05, 0x01,                     // Usage Page (Generic Desktop)
    0x09, 0x02,                     // Usage (Mouse)
    0xA1, 0x01,                     // Collection (Application)
    0x85, 0x02,                     //   Report ID (2)
    0x09, 0x01,                     //   Usage (Pointer)
    0xA1, 0x00,                     //   Collection (Physical)
    0x05, 0x09,                     //     Usage Page (Button)
    0x19, 0x01,                     //     Usage Minimum (Button 1)
    0x29, 0x03,                     //     Usage Maximum (Button 3)
    0x15, 0x00,                     //     Logical Minimum (0)
    0x25, 0x01,                     //     Logical Maximum (1)
    0x95, 0x03,                     //     Report Count (3)
    0x75, 0x01,                     //     Report Size (1)
    0x81, 0x02,                     //     Input (Data, Variable, Absolute)
    0x95, 0x01,                     //     Report Count (1)
    0x75, 0x05,                     //     Report Size (5)
    0x81, 0x01,                     //     Input (Constant)
    0x05, 0x01,                     //     Usage Page (Generic Desktop)
    0x09, 0x30,                     //     Usage (X)
    0x09, 0x31,                     //     Usage (Y)
    0x09, 0x38,                     //     Usage (Wheel)
    0x15, 0x81,                     //     Logical Minimum (-127)
    0x25, 0x7F,                     //     Logical Maximum (127)
    0x75, 0x08,                     //     Report Size (8)
    0x95, 0x03,                     //     Report Count (3)
    0x81, 0x06,                     //     Input (Data, Variable, Relative)
    0xC0,                           //   End Collection
    0xC0                            // End Collection
};

// HID report map for keyboard and mouse
static esp_hid_raw_report_map_t ble_report_maps[] = {
    {
        .data = hid_report_map,
        .len = sizeof(hid_report_map)
    }
};

// HID device configuration
static esp_hid_device_config_t hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "HID Bridge",
    .manufacturer_name = "efog.tech",
    .serial_number = "1234567890",
    .report_maps = ble_report_maps,
    .report_maps_len = 1
};

// Forward declarations
static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);

// GAP event handler
void esp_hidd_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT");
        esp_ble_gap_start_advertising(&hidd_adv_params);
        break;
    case ESP_GAP_BLE_SEC_REQ_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_SEC_REQ_EVT");
        for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
            ESP_LOGD(TAG, "%x:", param->ble_security.ble_req.bd_addr[i]);
        }
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
        break;
    case ESP_GAP_BLE_AUTH_CMPL_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_AUTH_CMPL_EVT");
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "remote BD_ADDR: %02x:%02x:%02x:%02x:%02x:%02x",
                bd_addr[0], bd_addr[1], bd_addr[2], bd_addr[3], bd_addr[4], bd_addr[5]);
        ESP_LOGI(TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
        ESP_LOGI(TAG, "pair status = %s", param->ble_security.auth_cmpl.success ? "success" : "fail");
        if (!param->ble_security.auth_cmpl.success) {
            ESP_LOGE(TAG, "fail reason = 0x%x", param->ble_security.auth_cmpl.fail_reason);
        }
        break;
    default:
        break;
    }
}

// GATTS event handler
extern void esp_hidd_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

esp_err_t ble_hid_device_init(void)
{
    if (s_ble_hid_device_initialized) {
        ESP_LOGW(TAG, "BLE HID device already initialized");
        return ESP_OK;
    }

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Release the memory for classic BT since we only use BLE
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize BT controller with BLE mode
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "Initialize BT controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "Enable BT controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "Initialize Bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "Enable Bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set device name
    ret = esp_bt_dev_set_device_name(hid_config.device_name);
    if (ret) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GAP callback
    ret = esp_ble_gap_register_callback(esp_hidd_gap_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "Register GAP callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register GATT callback
    ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler);
    if (ret) {
        ESP_LOGE(TAG, "Register GATT callback failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Initialize HID device profile
    ret = esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE, ble_hidd_event_callback, &s_hid_dev);
    if (ret) {
        ESP_LOGE(TAG, "Initialize HID device profile failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set security parameters
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));

    s_ble_hid_device_initialized = true;
    ESP_LOGI(TAG, "BLE HID device initialized");
    return ESP_OK;
}

esp_err_t ble_hid_device_deinit(void)
{
    if (!s_ble_hid_device_initialized) {
        ESP_LOGW(TAG, "BLE HID device not initialized");
        return ESP_OK;
    }

    // Deinitialize HID device profile
    esp_err_t ret = esp_hidd_dev_deinit(s_hid_dev);
    if (ret) {
        ESP_LOGE(TAG, "Deinitialize HID device profile failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Disable and deinitialize Bluedroid
    ret = esp_bluedroid_disable();
    if (ret) {
        ESP_LOGE(TAG, "Disable Bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_deinit();
    if (ret) {
        ESP_LOGE(TAG, "Deinitialize Bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Disable and deinitialize BT controller
    ret = esp_bt_controller_disable();
    if (ret) {
        ESP_LOGE(TAG, "Disable BT controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_deinit();
    if (ret) {
        ESP_LOGE(TAG, "Deinitialize BT controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_ble_hid_device_initialized = false;
    s_ble_hid_device_connected = false;
    s_hid_dev = NULL;

    ESP_LOGI(TAG, "BLE HID device deinitialized");
    return ESP_OK;
}

esp_err_t ble_hid_device_start_advertising(void)
{
    if (!s_ble_hid_device_initialized) {
        ESP_LOGE(TAG, "BLE HID device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ble_hid_device_connected) {
        ESP_LOGW(TAG, "BLE HID device already connected");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting BLE advertising");
    return esp_ble_gap_start_advertising(&hidd_adv_params);
}

esp_err_t ble_hid_device_stop_advertising(void)
{
    if (!s_ble_hid_device_initialized) {
        ESP_LOGE(TAG, "BLE HID device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Stopping BLE advertising");
    return esp_ble_gap_stop_advertising();
}

bool ble_hid_device_connected(void)
{
    return s_ble_hid_device_connected;
}

esp_err_t ble_hid_device_send_keyboard_report(usb_hid_keyboard_report_t *keyboard_report)
{
    if (!s_ble_hid_device_initialized) {
        ESP_LOGE(TAG, "BLE HID device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_ble_hid_device_connected) {
        ESP_LOGW(TAG, "BLE HID device not connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (keyboard_report == NULL) {
        ESP_LOGE(TAG, "Keyboard report is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Report ID 1 for keyboard
    uint8_t report_data[9];
    report_data[0] = 1; // Report ID
    memcpy(&report_data[1], keyboard_report, sizeof(usb_hid_keyboard_report_t));

    esp_err_t ret = esp_hidd_dev_input_set(s_hid_dev, 0, 1, report_data, sizeof(report_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send keyboard report failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ble_hid_device_send_mouse_report(usb_hid_mouse_report_t *mouse_report)
{
    if (!s_ble_hid_device_initialized) {
        ESP_LOGE(TAG, "BLE HID device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_ble_hid_device_connected) {
        ESP_LOGW(TAG, "BLE HID device not connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (mouse_report == NULL) {
        ESP_LOGE(TAG, "Mouse report is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Report ID 2 for mouse
    uint8_t report_data[5];
    report_data[0] = 2; // Report ID
    memcpy(&report_data[1], mouse_report, sizeof(usb_hid_mouse_report_t));

    esp_err_t ret = esp_hidd_dev_input_set(s_hid_dev, 0, 2, report_data, sizeof(report_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send mouse report failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
        case ESP_HIDD_START_EVENT:
            ESP_LOGI(TAG, "BLE HID device profile started");
            // Start advertising
            ble_hid_device_start_advertising();
            break;

        case ESP_HIDD_CONNECT_EVENT:
            ESP_LOGI(TAG, "BLE HID device connected");
            s_ble_hid_device_connected = true;
            s_hid_dev = param->connect.dev;
            break;

        case ESP_HIDD_DISCONNECT_EVENT:
            ESP_LOGI(TAG, "BLE HID device disconnected");
            s_ble_hid_device_connected = false;
            // Restart advertising
            ble_hid_device_start_advertising();
            break;

        case ESP_HIDD_STOP_EVENT:
            ESP_LOGI(TAG, "BLE HID device profile stopped");
            break;

        default:
            ESP_LOGW(TAG, "Unhandled BLE HID event: %d", event);
            break;
    }
}
