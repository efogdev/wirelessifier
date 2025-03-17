#include "ble_hid_device.h"
#include "../const.h"
#include <esp_gatt_common_api.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_hidd_prf_api.h"
#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "hid_dev.h"
#include "../utils/storage.h"

static const char *TAG = "BLE_HID";

static uint16_t s_conn_id = 0;
static bool s_connected = false;
static int s_reconnect_delay = 3; // Default reconnect delay in seconds
static uint8_t hidd_service_uuid128[] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    // first uuid, 16bit, [12],[13] is the value
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};

static esp_ble_adv_data_t hidd_adv_data = {
    .set_scan_rsp = false,
    .include_name = true,
    .include_txpower = true,
    .min_interval = 0x6, // slave connection min interval, Time = min_interval * 1.25 msec
    .max_interval = 0x80, // slave connection max interval, Time = max_interval * 1.25 msec
    .appearance = ESP_BLE_APPEARANCE_HID_MOUSE,
    .manufacturer_len = 0,
    .p_manufacturer_data =  NULL,
    .service_data_len = 0,
    .p_service_data = NULL,
    .service_uuid_len = sizeof(hidd_service_uuid128),
    .p_service_uuid = hidd_service_uuid128,
    .flag = 0x6,
};

static esp_ble_adv_params_t hidd_adv_params = {
    .adv_int_min        = 0x20,
    .adv_int_max        = 0x30,
    .adv_type           = ADV_TYPE_IND,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .channel_map        = ADV_CHNL_ALL,
    .adv_filter_policy  = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static void hidd_event_callback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t *param)
{
    switch(event) {
        case ESP_HIDD_EVENT_REG_FINISH: {
            if (param->init_finish.state == ESP_HIDD_INIT_OK) {
                // Get device name from settings
                char device_name[32];
                if (storage_get_string_setting("deviceInfo.name", device_name, sizeof(device_name)) != ESP_OK) {
                    // Fallback to default name from const.h
                    strcpy(device_name, DEVICE_NAME);
                }
                esp_ble_gap_set_device_name(device_name);
                esp_ble_gap_config_adv_data(&hidd_adv_data);
            }
            break;
        }
        case ESP_BAT_EVENT_REG: 
            break;
        case ESP_HIDD_EVENT_DEINIT_FINISH:
	        break;
		case ESP_HIDD_EVENT_BLE_CONNECT: {
            ESP_LOGI(TAG, "ESP_HIDD_EVENT_BLE_CONNECT");
            s_conn_id = param->connect.conn_id;
            s_connected = true;
            break;
        }
        case ESP_HIDD_EVENT_BLE_DISCONNECT: {
            s_connected = false;
            ESP_LOGI(TAG, "ESP_HIDD_EVENT_BLE_DISCONNECT");
            // Use reconnect delay from settings
            ESP_LOGI(TAG, "Will reconnect in %d seconds", s_reconnect_delay);
            vTaskDelay(pdMS_TO_TICKS(s_reconnect_delay * 1000));
            esp_ble_gap_start_advertising(&hidd_adv_params);
            break;
        }
        case ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT: {
            ESP_LOGI(TAG, "%s, ESP_HIDD_EVENT_BLE_VENDOR_REPORT_WRITE_EVT", __func__);
            ESP_LOG_BUFFER_HEX(TAG, param->vendor_write.data, param->vendor_write.length);
            break;
        }
        case ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT: {
            ESP_LOGI(TAG, "ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT");
            ESP_LOG_BUFFER_HEX(TAG, param->led_write.data, param->led_write.length);
            break;
        }
        default:
            break;
    }
}

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        esp_ble_gap_start_advertising(&hidd_adv_params);
        break;
     case ESP_GAP_BLE_SEC_REQ_EVT:
        esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
	 break;
     case ESP_GAP_BLE_AUTH_CMPL_EVT:
        esp_bd_addr_t bd_addr;
        memcpy(bd_addr, param->ble_security.auth_cmpl.bd_addr, sizeof(esp_bd_addr_t));
        ESP_LOGI(TAG, "remote BD_ADDR: %08x%04x",\
                (bd_addr[0] << 24) + (bd_addr[1] << 16) + (bd_addr[2] << 8) + bd_addr[3],
                (bd_addr[4] << 8) + bd_addr[5]);
        ESP_LOGI(TAG, "address type = %d", param->ble_security.auth_cmpl.addr_type);
        ESP_LOGI(TAG, "pair status = %s",param->ble_security.auth_cmpl.success ? "success" : "fail");
        if(!param->ble_security.auth_cmpl.success) {
            ESP_LOGE(TAG, "fail reason = 0x%x",param->ble_security.auth_cmpl.fail_reason);
            if(param->ble_security.auth_cmpl.fail_reason == 0x66) {
                ESP_LOGI(TAG, "Unbonding device due to error 0x66");
                esp_ble_remove_bond_device(bd_addr);
            }
        }
        break;
    default:
        break;
    }
}

esp_err_t ble_hid_device_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize settings
    init_global_settings();
    
    // Get BLE reconnect delay from settings
    int reconnect_delay;
    if (storage_get_int_setting("connectivity.bleReconnectDelay", &reconnect_delay) == ESP_OK) {
        s_reconnect_delay = reconnect_delay;
        ESP_LOGI(TAG, "BLE reconnect delay set to %d seconds", s_reconnect_delay);
    }

    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) {
        ESP_LOGE(TAG, "%s initialize controller failed", __func__);
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG, "%s enable controller failed", __func__);
        return ret;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG, "%s init bluedroid failed", __func__);
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG, "%s init bluedroid failed", __func__);
        return ret;
    }

    if((ret = esp_hidd_profile_init()) != ESP_OK) {
        ESP_LOGE(TAG, "%s init bluedroid failed", __func__);
        return ret;
    }

    esp_ble_gap_register_callback(gap_event_handler);
    esp_hidd_register_callbacks(hidd_event_callback);

    /* set the security iocap & auth_req & key size & init key response key parameters to the stack*/
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;           //set the IO capability to No output No input
    uint8_t key_size = 16;      //the key size should be 7~16 bytes
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t));
    // Set TX power based on settings
    char tx_power_str[10];
    esp_power_level_t power_level = ESP_PWR_LVL_N0;
    
    if (storage_get_string_setting("connectivity.bleTxPower", tx_power_str, sizeof(tx_power_str)) == ESP_OK) {
        ESP_LOGI(TAG, "BLE TX power setting: %s", tx_power_str);
        if (strcmp(tx_power_str, "n6") == 0) {
            power_level = ESP_PWR_LVL_N6;
        } else if (strcmp(tx_power_str, "n3") == 0) {
            power_level = ESP_PWR_LVL_N3;
        } else if (strcmp(tx_power_str, "n0") == 0) {
            power_level = ESP_PWR_LVL_N0;
        } else if (strcmp(tx_power_str, "p3") == 0) {
            power_level = ESP_PWR_LVL_P3;
        } else if (strcmp(tx_power_str, "p6") == 0) {
            power_level = ESP_PWR_LVL_P6;
        } else if (strcmp(tx_power_str, "p9") == 0) {
            power_level = ESP_PWR_LVL_P9;
        }
    }
    
    ESP_LOGI(TAG, "Setting BLE TX power to level: %d", power_level);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, power_level);

    bool low_power_mode = false;
    storage_get_bool_setting("power.lowPowerMode", &low_power_mode);

    if (low_power_mode) {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N6);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_N6);
    } else {
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, power_level);
        esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, power_level);
    }

    esp_ble_gatt_set_local_mtu(120);
    return ESP_OK;
}

esp_err_t ble_hid_device_deinit(void)
{
    esp_err_t ret;

    if ((ret = esp_hidd_profile_deinit()) != ESP_OK) {
        return ret;
    }

    if ((ret = esp_bluedroid_disable()) != ESP_OK) {
        return ret;
    }

    if ((ret = esp_bluedroid_deinit()) != ESP_OK) {
        return ret;
    }

    if ((ret = esp_bt_controller_disable()) != ESP_OK) {
        return ret;
    }

    if ((ret = esp_bt_controller_deinit()) != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t ble_hid_device_start_advertising(void)
{
    // Get device name from settings
    char device_name[32];
    if (storage_get_string_setting("deviceInfo.name", device_name, sizeof(device_name)) != ESP_OK) {
        // Fallback to default name from const.h
        #include "../const.h"
        strcpy(device_name, DEVICE_NAME);
    }
    
    ESP_LOGI(TAG, "Advertising with device name: %s", device_name);
    esp_ble_gap_set_device_name(device_name);
    esp_err_t ret = esp_ble_gap_config_adv_data(&hidd_adv_data);
    if (ret) {
        ESP_LOGE(TAG, "config adv data failed, error code = %x", ret);
        return ret;
    }

    return ESP_OK;
}

bool ble_hid_device_connected(void)
{
    return s_connected;
}

esp_err_t ble_hid_device_send_keyboard_report(const keyboard_report_t *report)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t buffer[8];
    buffer[0] = report->modifier;
    buffer[1] = report->reserved;
    memcpy(&buffer[2], report->keycode, 6);

    esp_hidd_send_keyboard_value(s_conn_id, 0, buffer, 8);
    return ESP_OK;
}

esp_err_t ble_hid_device_send_mouse_report(const mouse_report_t *report)
{
    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_hidd_send_mouse_value(s_conn_id, report->buttons, report->x, report->y, report->wheel);
    return ESP_OK;
}
