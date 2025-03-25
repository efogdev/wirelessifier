#include "esp_hidd_prf_api.h"
#include "hid_device_le_prf.h"
#include "hid_dev.h"
#include <string.h>
#include "esp_log.h"

#define HID_KEYBOARD_IN_RPT_LEN     32
#define HID_MOUSE_IN_RPT_LEN        7

esp_err_t esp_hidd_register_callbacks(esp_hidd_event_cb_t callbacks) {
    esp_err_t hidd_status;

    if (callbacks != NULL) {
        hidd_le_env.hidd_cb = callbacks;
    } else {
        return ESP_FAIL;
    }

    if ((hidd_status = hidd_register_cb()) != ESP_OK) {
        return hidd_status;
    }

    esp_ble_gatts_app_register(BATTRAY_APP_ID);

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

    esp_ble_gatts_app_unregister(hidd_le_env.gatt_if);
    hidd_le_env.enabled = false;
    return ESP_OK;
}

uint16_t esp_hidd_get_version(void) {
    return HIDD_VERSION;
}

void esp_hidd_send_keyboard_value(const uint16_t conn_id, const key_mask_t special_key_mask, const uint8_t *keyboard_cmd,
                                  const uint8_t num_key) {
    if (num_key > HID_KEYBOARD_IN_RPT_LEN - 2) {
        ESP_LOGE(HID_LE_PRF_TAG, "%s(), the number key should not be more than %d", __func__, HID_KEYBOARD_IN_RPT_LEN);
        return;
    }

    uint8_t buffer[HID_KEYBOARD_IN_RPT_LEN] = {0};
    buffer[0] = special_key_mask;
    for (int i = 0; i < num_key; i++) {
        buffer[i + 2] = keyboard_cmd[i];
    }

    hid_dev_send_report(hidd_le_env.gatt_if,
        conn_id, HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT, HID_KEYBOARD_IN_RPT_LEN, buffer);
}

__attribute__((section(".iram1.text"))) void esp_hidd_send_mouse_value(const uint16_t conn_id, const uint8_t mouse_button, const uint16_t mickeys_x,
                               const uint16_t mickeys_y, const int8_t wheel, const int8_t pan) {
    uint8_t buffer[HID_MOUSE_IN_RPT_LEN];
    buffer[0] = mickeys_x & 0xFF;
    buffer[1] = (mickeys_x >> 8);
    buffer[2] = mickeys_y & 0xFF;
    buffer[3] = (mickeys_y >> 8);
    buffer[4] = wheel;
    buffer[5] = pan;
    buffer[6] = mouse_button;

    hid_dev_send_report(hidd_le_env.gatt_if, conn_id, HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT, HID_MOUSE_IN_RPT_LEN,
                        buffer);
}
