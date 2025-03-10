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
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gatt_common_api.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

static const char *TAG = "BLE_HID_DEVICE";
static bool s_ble_hid_device_initialized = false;
static bool s_ble_hid_device_connected = false;
static esp_hidd_dev_t *s_hid_dev = NULL;
static uint16_t s_hid_conn_id = 0;

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x30,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static uint8_t hidd_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

// BLE advertising data
static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x0006, // Slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x0010, // Slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = 0x03c0,   // HID Generic
    .manufacturer_len = 0,
    .p_manufacturer_data = NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

// Event group to signal when the device is ready
static EventGroupHandle_t s_ble_event_group;
#define BLE_CONNECTED_BIT      BIT0
#define BLE_DISCONNECTED_BIT   BIT1

// HID report map for keyboard, mouse, and consumer control
static const uint8_t hid_report_map[] = {
    // Mouse report descriptor
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  // Report Id (1)
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Buttons)
    0x19, 0x01,  //     Usage Minimum (01) - Button 1
    0x29, 0x03,  //     Usage Maximum (03) - Button 3
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x75, 0x01,  //     Report Size (1)
    0x95, 0x03,  //     Report Count (3)
    0x81, 0x02,  //     Input (Data, Variable, Absolute) - Button states
    0x75, 0x05,  //     Report Size (5)
    0x95, 0x01,  //     Report Count (1)
    0x81, 0x01,  //     Input (Constant) - Padding or Reserved bits
    0x05, 0x01,  //     Usage Page (Generic Desktop)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x09, 0x38,  //     Usage (Wheel)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x03,  //     Report Count (3)
    0x81, 0x06,  //     Input (Data, Variable, Relative) - X & Y coordinate
    0xC0,        //   End Collection
    0xC0,        // End Collection

    // Keyboard report descriptor
    0x05, 0x01,  // Usage Pg (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection: (Application)
    0x85, 0x02,  // Report Id (2)
    //
    0x05, 0x07,  //   Usage Pg (Key Codes)
    0x19, 0xE0,  //   Usage Min (224)
    0x29, 0xE7,  //   Usage Max (231)
    0x15, 0x00,  //   Log Min (0)
    0x25, 0x01,  //   Log Max (1)
    //
    //   Modifier byte
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input: (Data, Variable, Absolute)
    //
    //   Reserved byte
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input: (Constant)
    //
    //   LED report
    0x05, 0x08,  //   Usage Pg (LEDs)
    0x19, 0x01,  //   Usage Min (1)
    0x29, 0x05,  //   Usage Max (5)
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x91, 0x02,  //   Output: (Data, Variable, Absolute)
    //
    //   LED report padding
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output: (Constant)
    //
    //   Key arrays (6 bytes)
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Log Min (0)
    0x25, 0x65,  //   Log Max (101)
    0x05, 0x07,  //   Usage Pg (Key Codes)
    0x19, 0x00,  //   Usage Min (0)
    0x29, 0x65,  //   Usage Max (101)
    0x81, 0x00,  //   Input: (Data, Array)
    //
    0xC0,        // End Collection
    
    // Consumer Control report descriptor
    0x05, 0x0C,   // Usage Pg (Consumer Devices)
    0x09, 0x01,   // Usage (Consumer Control)
    0xA1, 0x01,   // Collection (Application)
    0x85, 0x03,   // Report Id (3)
    0x09, 0x02,   //   Usage (Numeric Key Pad)
    0xA1, 0x02,   //   Collection (Logical)
    0x05, 0x09,   //     Usage Pg (Button)
    0x19, 0x01,   //     Usage Min (Button 1)
    0x29, 0x0A,   //     Usage Max (Button 10)
    0x15, 0x01,   //     Logical Min (1)
    0x25, 0x0A,   //     Logical Max (10)
    0x75, 0x04,   //     Report Size (4)
    0x95, 0x01,   //     Report Count (1)
    0x81, 0x00,   //     Input (Data, Ary, Abs)
    0xC0,         //   End Collection
    0x05, 0x0C,   //   Usage Pg (Consumer Devices)
    0x09, 0x86,   //   Usage (Channel)
    0x15, 0xFF,   //   Logical Min (-1)
    0x25, 0x01,   //   Logical Max (1)
    0x75, 0x02,   //   Report Size (2)
    0x95, 0x01,   //   Report Count (1)
    0x81, 0x46,   //   Input (Data, Var, Rel, Null)
    0x09, 0xE9,   //   Usage (Volume Up)
    0x09, 0xEA,   //   Usage (Volume Down)
    0x15, 0x00,   //   Logical Min (0)
    0x75, 0x01,   //   Report Size (1)
    0x95, 0x02,   //   Report Count (2)
    0x81, 0x02,   //   Input (Data, Var, Abs)
    0x09, 0xE2,   //   Usage (Mute)
    0x09, 0x30,   //   Usage (Power)
    0x09, 0x83,   //   Usage (Recall Last)
    0x09, 0x81,   //   Usage (Assign Selection)
    0x09, 0xB0,   //   Usage (Play)
    0x09, 0xB1,   //   Usage (Pause)
    0x09, 0xB2,   //   Usage (Record)
    0x09, 0xB3,   //   Usage (Fast Forward)
    0x09, 0xB4,   //   Usage (Rewind)
    0x09, 0xB5,   //   Usage (Scan Next)
    0x09, 0xB6,   //   Usage (Scan Prev)
    0x09, 0xB7,   //   Usage (Stop)
    0x15, 0x01,   //   Logical Min (1)
    0x25, 0x0C,   //   Logical Max (12)
    0x75, 0x04,   //   Report Size (4)
    0x95, 0x01,   //   Report Count (1)
    0x81, 0x00,   //   Input (Data, Ary, Abs)
    0x09, 0x80,   //   Usage (Selection)
    0xA1, 0x02,   //   Collection (Logical)
    0x05, 0x09,   //     Usage Pg (Button)
    0x19, 0x01,   //     Usage Min (Button 1)
    0x29, 0x03,   //     Usage Max (Button 3)
    0x15, 0x01,   //     Logical Min (1)
    0x25, 0x03,   //     Logical Max (3)
    0x75, 0x02,   //     Report Size (2)
    0x81, 0x00,   //     Input (Data, Ary, Abs)
    0xC0,         //   End Collection
    0x81, 0x03,   //   Input (Const, Var, Abs)
    0xC0,         // End Collection
};

// HID report map for keyboard, mouse, and consumer control
static esp_hid_raw_report_map_t ble_report_maps[] = {
    {
        .data = hid_report_map,
        .len = sizeof(hid_report_map)
    }
};

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

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);

void esp_hidd_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT");
        esp_ble_gap_start_advertising(&hidd_adv_params);
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_START_COMPLETE_EVT, status=%d", param->adv_start_cmpl.status);
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising start failed");
        } else {
            ESP_LOGI(TAG, "Advertising started successfully");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG, "ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT, status=%d", param->adv_stop_cmpl.status);
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "Advertising stop failed");
        } else {
            ESP_LOGI(TAG, "Advertising stopped successfully");
        }
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
        } else {
            s_ble_hid_device_connected = true;
            xEventGroupSetBits(s_ble_event_group, BLE_CONNECTED_BIT);
        }
        break;
    default:
        break;
    }
}

// GATTS event handler
void esp_hidd_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

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

// Helper function to build consumer control report
static void build_consumer_report(uint8_t *buffer, consumer_cmd_t cmd, bool key_pressed)
{
    if (!buffer) {
        ESP_LOGE(TAG, "Buffer is NULL, cannot build consumer report");
        return;
    }

    // Clear the buffer
    memset(buffer, 0, 2);

    if (!key_pressed) {
        return; // All zeros for key release
    }

    switch (cmd) {
        case HID_CONSUMER_CHANNEL_UP:
            buffer[0] = 0x10; // Channel up
            break;
        case HID_CONSUMER_CHANNEL_DOWN:
            buffer[0] = 0x30; // Channel down
            break;
        case HID_CONSUMER_VOLUME_UP:
            buffer[0] = 0x40; // Volume up
            break;
        case HID_CONSUMER_VOLUME_DOWN:
            buffer[0] = 0x80; // Volume down
            break;
        case HID_CONSUMER_MUTE:
            buffer[1] = 0x01; // Mute
            break;
        case HID_CONSUMER_POWER:
            buffer[1] = 0x02; // Power
            break;
        case HID_CONSUMER_RECALL_LAST:
            buffer[1] = 0x03; // Recall last
            break;
        case HID_CONSUMER_ASSIGN_SEL:
            buffer[1] = 0x04; // Assign selection
            break;
        case HID_CONSUMER_PLAY:
            buffer[1] = 0x05; // Play
            break;
        case HID_CONSUMER_PAUSE:
            buffer[1] = 0x06; // Pause
            break;
        case HID_CONSUMER_RECORD:
            buffer[1] = 0x07; // Record
            break;
        case HID_CONSUMER_FAST_FORWARD:
            buffer[1] = 0x08; // Fast forward
            break;
        case HID_CONSUMER_REWIND:
            buffer[1] = 0x09; // Rewind
            break;
        case HID_CONSUMER_SCAN_NEXT_TRK:
            buffer[1] = 0x0A; // Scan next track
            break;
        case HID_CONSUMER_SCAN_PREV_TRK:
            buffer[1] = 0x0B; // Scan previous track
            break;
        case HID_CONSUMER_STOP:
            buffer[1] = 0x0C; // Stop
            break;
        default:
            ESP_LOGW(TAG, "Unknown consumer command: %d", cmd);
            break;
    }
}

esp_err_t ble_hid_device_send_consumer_control(consumer_cmd_t cmd, bool key_pressed)
{
    if (!s_ble_hid_device_initialized) {
        ESP_LOGE(TAG, "BLE HID device not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_ble_hid_device_connected) {
        ESP_LOGW(TAG, "BLE HID device not connected");
        return ESP_ERR_INVALID_STATE;
    }

    // Report ID 3 for consumer control
    uint8_t report_data[3]; // 1 byte for report ID, 2 bytes for consumer control
    report_data[0] = 3; // Report ID
    
    // Build the consumer control report
    build_consumer_report(&report_data[1], cmd, key_pressed);

    esp_err_t ret = esp_hidd_dev_input_set(s_hid_dev, 0, 3, report_data, sizeof(report_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send consumer report failed: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ble_hid_device_init(void)
{
    if (s_ble_hid_device_initialized) {
        return ESP_OK;
    }

    // Create event group
    s_ble_event_group = xEventGroupCreate();
    if (s_ble_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    // Release the memory for classic BT since we only use BLE
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // Initialize BT controller with BLE mode
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&bt_cfg);
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
    ret = esp_ble_gap_set_device_name(hid_config.device_name);
    if (ret) {
        ESP_LOGE(TAG, "Set device name failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure advertising data
    ret = esp_ble_gap_config_adv_data(&hidd_adv_data);
    if (ret) {
        ESP_LOGE(TAG, "Configure advertising data failed: %s", esp_err_to_name(ret));
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

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidd_event_t event = (esp_hidd_event_t)id;
    esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;

    switch (event) {
        case ESP_HIDD_START_EVENT:
            ESP_LOGI(TAG, "BLE HID device profile started");
            ble_hid_device_start_advertising();
            break;

        case ESP_HIDD_CONNECT_EVENT:
            ESP_LOGI(TAG, "BLE HID device connected");
            s_ble_hid_device_connected = true;
            s_hid_dev = param->connect.dev;
            //s_hid_conn_id = param->connect.conn_id;
            xEventGroupSetBits(s_ble_event_group, BLE_CONNECTED_BIT);
            break;

        case ESP_HIDD_DISCONNECT_EVENT:
            ESP_LOGI(TAG, "BLE HID device disconnected");
            s_ble_hid_device_connected = false;
            xEventGroupSetBits(s_ble_event_group, BLE_DISCONNECTED_BIT);
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
