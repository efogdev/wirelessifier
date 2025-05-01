#include "esp_hidd_prf_api.h"
#include "hid_device_le_prf.h"
#include "hid_dev.h"
#include <string.h>
#include "esp_log.h"

#define HID_KEYBOARD_IN_RPT_LEN     63
#define HID_MOUSE_IN_RPT_LEN        7
#define HID_SYS_CTRL_IN_RPT_LEN     2
#define HID_CONSUMER_IN_RPT_LEN     2

static uint8_t s_report_buffer[HID_KEYBOARD_IN_RPT_LEN+2] __attribute__((section(".dram1.data")));
static bool s_enabled = true;

bool is_ble_enabled(void) {
    return s_enabled;
}

esp_err_t esp_hidd_register_callbacks(const esp_hidd_event_cb_t callbacks) {
    esp_err_t hidd_status;

    if (callbacks != NULL) {
        hidd_le_env.hidd_cb = callbacks;
    } else {
        return ESP_FAIL;
    }

    if ((hidd_status = hidd_register_cb()) != ESP_OK) {
        return hidd_status;
    }

    esp_ble_gatts_app_register(BATTERY_APP_ID);
    if ((hidd_status = esp_ble_gatts_app_register(HIDD_APP_ID)) != ESP_OK) {
        return hidd_status;
    }

    return hidd_status;
}

esp_err_t esp_hidd_profile_init(void) {
    if (hidd_le_env.enabled) {
        ESP_LOGE(HID_LE_PRF_TAG, "HID device profile already initialized");
        return ESP_FAIL;
    }

    s_enabled = true;
    memset(&hidd_le_env, 0, sizeof(hidd_le_env_t));
    hidd_le_env.enabled = true;
    return ESP_OK;
}

esp_err_t esp_hidd_profile_deinit(void) {
    const uint16_t hidd_svc_hdl = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC];
    if (!hidd_le_env.enabled) {
        ESP_LOGE(HID_LE_PRF_TAG, "HID device profile already deinitialized");
        return ESP_OK;
    }

    if (hidd_svc_hdl != 0) {
        esp_ble_gatts_stop_service(hidd_svc_hdl);
        esp_ble_gatts_delete_service(hidd_svc_hdl);
    } else {
        return ESP_FAIL;
    }

    s_enabled = false;
    esp_ble_gatts_app_unregister(hidd_le_env.gatt_if);
    hidd_le_env.enabled = false;
    return ESP_OK;
}

uint16_t esp_hidd_get_version(void) {
    return HIDD_VERSION;
}

void esp_hidd_send_keyboard_value(const uint16_t conn_id, const key_mask_t special_key_mask, const uint8_t *keyboard_cmd) {
    // ESP_LOGI(HID_LE_PRF_TAG, "mask=%02X data=%08X%08X", special_key_mask, *(const uint32_t *const)keyboard_cmd, *(const uint32_t*const)&keyboard_cmd[4]);

    s_report_buffer[0] = special_key_mask;
    memset(&s_report_buffer[1], 0, HID_KEYBOARD_IN_RPT_LEN - 1);
    for (int i = 1; i < HID_KEYBOARD_IN_RPT_LEN - 1; i++) {
        s_report_buffer[i + 1] = keyboard_cmd[i - 1];
    }

    hid_dev_send_report(hidd_le_env.gatt_if,
        conn_id, HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT, HID_KEYBOARD_IN_RPT_LEN, s_report_buffer);
}

void IRAM_ATTR esp_hidd_send_mouse_value(const uint16_t conn_id, const uint8_t mouse_button, const uint16_t mickeys_x,
                               const uint16_t mickeys_y, const int8_t wheel, const int8_t pan) {
    s_report_buffer[0] = mickeys_x & 0xFF;
    s_report_buffer[1] = (mickeys_x >> 8);
    s_report_buffer[2] = mickeys_y & 0xFF;
    s_report_buffer[3] = (mickeys_y >> 8);
    s_report_buffer[4] = wheel;
    s_report_buffer[5] = pan;
    s_report_buffer[6] = mouse_button;

    hid_dev_send_report(hidd_le_env.gatt_if, conn_id, HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT, HID_MOUSE_IN_RPT_LEN,
                        s_report_buffer);
}

void esp_hidd_send_system_control_value(const uint16_t conn_id, const uint16_t sys_ctrl) {
    s_report_buffer[0] = sys_ctrl & 0xFF;
    s_report_buffer[1] = (sys_ctrl >> 8);

    hid_dev_send_report(hidd_le_env.gatt_if, conn_id, HID_RPT_ID_SYS_IN, HID_REPORT_TYPE_INPUT, HID_SYS_CTRL_IN_RPT_LEN,
                        s_report_buffer);
}

void esp_hidd_send_consumer_value(const uint16_t conn_id, const uint16_t consumer_control) {
    s_report_buffer[0] = consumer_control & 0xFF;
    s_report_buffer[1] = (consumer_control >> 8);

    hid_dev_send_report(hidd_le_env.gatt_if, conn_id, HID_RPT_ID_CC_IN, HID_REPORT_TYPE_INPUT, HID_CONSUMER_IN_RPT_LEN,
                        s_report_buffer);
}
