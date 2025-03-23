#include "hid_device_le_prf.h"
#include <string.h>
#include "esp_log.h"
#include "storage.h"
#include "hid_report_data.h"

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
};

hidd_le_env_t hidd_le_env;
uint8_t hidProtocolMode = HID_PROTOCOL_MODE_REPORT;

static void hid_add_id_tbl(void);

void esp_hidd_prf_cb_hdl(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                         esp_ble_gatts_cb_param_t *param) {
    switch (event) {
        case ESP_GATTS_REG_EVT: {
            esp_ble_gap_config_local_icon(ESP_BLE_APPEARANCE_GENERIC_HID);
            esp_hidd_cb_param_t hidd_param;
            hidd_param.init_finish.state = param->reg.status;
            if (param->reg.app_id == HIDD_APP_ID) {
                hidd_le_env.gatt_if = gatts_if;
                if (hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_REG_FINISH, &hidd_param);
                    hidd_le_create_service(hidd_le_env.gatt_if);
                }
            }
            if (param->reg.app_id == BATTRAY_APP_ID) {
                hidd_param.init_finish.gatts_if = gatts_if;
                if (hidd_le_env.hidd_cb != NULL) {
                    (hidd_le_env.hidd_cb)(ESP_BAT_EVENT_REG, &hidd_param);
                }
            }

            break;
        }
        case ESP_GATTS_CONF_EVT: {
            break;
        }
        case ESP_GATTS_CREATE_EVT:
            break;
        case ESP_GATTS_CONNECT_EVT: {
            esp_hidd_cb_param_t cb_param = {0};
            ESP_LOGI(HID_LE_PRF_TAG, "HID connection established, conn_id = %x", param->connect.conn_id);
            memcpy(cb_param.connect.remote_bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
            cb_param.connect.conn_id = param->connect.conn_id;
            hidd_clcb_alloc(param->connect.conn_id, param->connect.remote_bda);
            esp_ble_set_encryption(param->connect.remote_bda, ESP_BLE_SEC_ENCRYPT_MITM);
            if (hidd_le_env.hidd_cb != NULL) {
                (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_CONNECT, &cb_param);
            }

            esp_ble_conn_update_params_t conn_params;
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));

            conn_params.latency = 0x00;
            conn_params.min_int = 0x06; // x 1.25ms
            conn_params.max_int = 0x06; // x 1.25ms
            conn_params.timeout = 0xA0; // x 6.25ms

            esp_ble_gap_update_conn_params(&conn_params);
            break;
        }
        case ESP_GATTS_DISCONNECT_EVT: {
            if (hidd_le_env.hidd_cb != NULL) {
                (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_DISCONNECT, NULL);
            }
            hidd_clcb_dealloc(param->disconnect.conn_id);
            break;
        }
        case ESP_GATTS_CLOSE_EVT:
            break;
        case ESP_GATTS_WRITE_EVT: {
            esp_hidd_cb_param_t cb_param = {0};
            if (param->write.handle == hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_LED_OUT_VAL]) {
                cb_param.led_write.conn_id = param->write.conn_id;
                cb_param.led_write.report_id = HID_RPT_ID_LED_OUT;
                cb_param.led_write.length = param->write.len;
                cb_param.led_write.data = param->write.value;
                (hidd_le_env.hidd_cb)(ESP_HIDD_EVENT_BLE_LED_REPORT_WRITE_EVT, &cb_param);
            }
            break;
        }
        case ESP_GATTS_CREAT_ATTR_TAB_EVT: {
            if (param->add_attr_tab.num_handle == BAS_IDX_NB &&
                param->add_attr_tab.svc_uuid.uuid.uuid16 == ESP_GATT_UUID_BATTERY_SERVICE_SVC &&
                param->add_attr_tab.status == ESP_GATT_OK) {
                incl_svc.start_hdl = param->add_attr_tab.handles[BAS_IDX_SVC];
                incl_svc.end_hdl = incl_svc.start_hdl + BAS_IDX_NB - 1;
                ESP_LOGI(HID_LE_PRF_TAG, "%s(), start added the hid service to the stack database. incl_handle = %d",
                         __func__, incl_svc.start_hdl);
                esp_ble_gatts_create_attr_tab(hidd_le_gatt_db, gatts_if, HIDD_LE_IDX_NB, 0);
            }
            if (param->add_attr_tab.num_handle == HIDD_LE_IDX_NB &&
                param->add_attr_tab.status == ESP_GATT_OK) {
                memcpy(hidd_le_env.hidd_inst.att_tbl, param->add_attr_tab.handles,
                       HIDD_LE_IDX_NB * sizeof(uint16_t));
                ESP_LOGI(HID_LE_PRF_TAG, "hid svc handle = %x", hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC]);
                hid_add_id_tbl();
                esp_ble_gatts_start_service(hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_SVC]);
            } else {
                esp_ble_gatts_start_service(param->add_attr_tab.handles[0]);
            }
            break;
        }

        default:
            break;
    }
}

void hidd_le_create_service(const esp_gatt_if_t gatts_if) {
    esp_ble_gatts_create_attr_tab(bas_att_db, gatts_if, BAS_IDX_NB, 0);
}

void hidd_le_init(void) {
    memset(&hidd_le_env, 0, sizeof(hidd_le_env_t));
}

void hidd_clcb_alloc(const uint16_t conn_id, esp_bd_addr_t bda) {
    uint8_t i_clcb = 0;
    hidd_clcb_t *p_clcb = NULL;

    for (i_clcb = 0, p_clcb = hidd_le_env.hidd_clcb; i_clcb < HID_MAX_APPS; i_clcb++, p_clcb++) {
        if (!p_clcb->in_use) {
            p_clcb->in_use = true;
            p_clcb->conn_id = conn_id;
            p_clcb->connected = true;
            memcpy(p_clcb->remote_bda, bda, ESP_BD_ADDR_LEN);
            break;
        }
    }
}

bool hidd_clcb_dealloc(uint16_t conn_id) {
    uint8_t i_clcb = 0;
    hidd_clcb_t *p_clcb = NULL;
    for (i_clcb = 0, p_clcb = hidd_le_env.hidd_clcb; i_clcb < HID_MAX_APPS; i_clcb++, p_clcb++) {
        memset(p_clcb, 0, sizeof(hidd_clcb_t));
        return true;
    }

    return false;
}

static struct gatts_profile_inst heart_rate_profile_tab[PROFILE_NUM] = {
    [PROFILE_APP_IDX] = {
        .gatts_cb = esp_hidd_prf_cb_hdl,
        .gatts_if = ESP_GATT_IF_NONE,
    },

};

static void gatts_event_handler(const esp_gatts_cb_event_t event, const esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param) {
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            heart_rate_profile_tab[PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGI(HID_LE_PRF_TAG, "Reg app failed, app_id %04x, status %d",
                     param->reg.app_id,
                     param->reg.status);
            return;
        }
    }

    do {
        for (int idx = 0; idx < PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE ||
                gatts_if == heart_rate_profile_tab[idx].gatts_if) {
                if (heart_rate_profile_tab[idx].gatts_cb) {
                    heart_rate_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}


esp_err_t hidd_register_cb(void) {
    return esp_ble_gatts_register_callback(gatts_event_handler);
}

void hidd_set_attr_value(const uint16_t handle, const uint16_t val_len, const uint8_t *value) {
    const hidd_inst_t *hidd_inst = &hidd_le_env.hidd_inst;
    if (hidd_inst->att_tbl[HIDD_LE_IDX_HID_INFO_VAL] <= handle &&
        hidd_inst->att_tbl[HIDD_LE_IDX_REPORT_REP_REF] >= handle) {
        esp_ble_gatts_set_attr_value(handle, val_len, value);
    } else {
        ESP_LOGE(HID_LE_PRF_TAG, "%s error:Invalid handle value.", __func__);
    }
}

void hidd_get_attr_value(const uint16_t handle, uint16_t *length, uint8_t **value) {
    const hidd_inst_t *hidd_inst = &hidd_le_env.hidd_inst;
    if (hidd_inst->att_tbl[HIDD_LE_IDX_HID_INFO_VAL] <= handle &&
        hidd_inst->att_tbl[HIDD_LE_IDX_REPORT_REP_REF] >= handle) {
        esp_ble_gatts_get_attr_value(handle, length, (const uint8_t **)value);
    } else {
        ESP_LOGE(HID_LE_PRF_TAG, "%s error:Invalid handle value.", __func__);
    }
}

static void hid_add_id_tbl(void) {
    // Mouse input report
    hid_rpt_map[0].id = hidReportRefMouseIn[0];
    hid_rpt_map[0].type = hidReportRefMouseIn[1];
    hid_rpt_map[0].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_VAL];
    hid_rpt_map[0].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_MOUSE_IN_CCC];
    hid_rpt_map[0].mode = HID_PROTOCOL_MODE_REPORT;

    // System Control input report
    hid_rpt_map[1].id = hidReportRefSysCtrlIn[0];
    hid_rpt_map[1].type = hidReportRefSysCtrlIn[1];
    hid_rpt_map[1].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_SYS_CTRL_IN_VAL];
    hid_rpt_map[1].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_SYS_CTRL_IN_CCC];
    hid_rpt_map[1].mode = HID_PROTOCOL_MODE_REPORT;

    // Consumer Control input report
    hid_rpt_map[2].id = hidReportRefConsumerIn[0];
    hid_rpt_map[2].type = hidReportRefConsumerIn[1];
    hid_rpt_map[2].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_CONSUMER_IN_VAL];
    hid_rpt_map[2].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_CONSUMER_IN_CCC];
    hid_rpt_map[2].mode = HID_PROTOCOL_MODE_REPORT;

    // Key input report
    hid_rpt_map[3].id = hidReportRefKeyIn[0];
    hid_rpt_map[3].type = hidReportRefKeyIn[1];
    hid_rpt_map[3].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_KEY_IN_VAL];
    hid_rpt_map[3].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_KEY_IN_CCC];
    hid_rpt_map[3].mode = HID_PROTOCOL_MODE_REPORT;

    // Setup report ID map
    hid_dev_register_reports(4, hid_rpt_map);
}
