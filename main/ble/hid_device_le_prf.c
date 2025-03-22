/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "hid_device_le_prf.h"
#include <string.h>
#include "esp_log.h"
#include "../utils/storage.h"

/// characteristic presentation information
struct prf_char_pres_fmt {
    /// Unit (The Unit is a UUID)
    uint16_t unit;
    /// Description
    uint16_t description;
    /// Format
    uint8_t format;
    /// Exponent
    uint8_t exponent;
    /// Name space
    uint8_t name_space;
};

// HID report mapping table
static hid_report_map_t hid_rpt_map[HID_NUM_REPORTS];

static const uint16_t hid_ccc_default = 0x0100;

// HID Report Map characteristic value
// Comprehensive HID descriptor supporting multiple report types

/// Battery Service Attributes Indexes
enum {
    BAS_IDX_SVC,
    BAS_IDX_BATT_LVL_CHAR,
    BAS_IDX_BATT_LVL_VAL,
    BAS_IDX_BATT_LVL_NTF_CFG,
    BAS_IDX_BATT_LVL_PRES_FMT,
    BAS_IDX_NB,
};

static const uint8_t hidReportMap[] = {
    // Mouse Report Descriptor
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x02, // Usage (Mouse)
    0xA1, 0x01, // Collection (Application)
    0x85, 0x01, // Report Id (1)
    0x09, 0x01, // Usage (Pointer)
    0xA1, 0x00, // Collection (Physical)
    // X, Y
    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x30, // Usage (X)
    0x09, 0x31, // Usage (Y)
    0x95, 0x02, // Report Count (2)
    0x75, 0x10, // Report Size (16)
    0x16, 0x00, 0x80, // Logical Minimum (-32768)
    0x26, 0xFF, 0x7F, // Logical Maximum (32767)
    0x81, 0x06, // Input (Data, Variable, Relative)
    // Vertical Wheel
    0x09, 0x38, // Usage (Wheel)
    0x95, 0x01, // Report Count (1)
    0x75, 0x08, // Report Size (8)
    0x15, 0x81, // Logical Minimum (-127)
    0x25, 0x7F, // Logical Maximum (127)
    0x81, 0x06, // Input (Data, Variable, Relative)
    // Horizontal Wheel
    0x05, 0x0C, // Usage Page (Consumer)
    0x0A, 0x38, 0x02, // Usage (AC Pan)
    0x95, 0x01, // Report Count (1)
    0x75, 0x08, // Report Size (8)
    0x15, 0x81, // Logical Minimum (-127)
    0x25, 0x7F, // Logical Maximum (127)
    0x81, 0x06, // Input (Data, Variable, Relative)
    // Buttons
    0x05, 0x09, // Usage Page (Buttons)
    0x19, 0x01, // Usage Minimum (01) - Button 1
    0x29, 0x08, // Usage Maximum (08) - Button 8
    0x95, 0x08, // Report Count (8)
    0x75, 0x01, // Report Size (1)
    0x15, 0x00, // Logical Minimum (0)
    0x25, 0x01, // Logical Maximum (1)
    0x81, 0x02, // Input (Data, Variable, Absolute)
    0xC0, //   End Collection
    0xC0, // End Collection

    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x80, // Usage (System Control)
    0xa1, 0x01, // Collection (Application)
    0x85, 0x03, //  Report ID (3)
    0x19, 0x01, //  Usage Minimum (1)
    0x2a, 0xb7, 0x00, //  Usage Maximum (183)
    0x15, 0x01, //  Logical Minimum (1)
    0x26, 0xb7, 0x00, //  Logical Maximum (183)
    0x95, 0x01, //  Report Count (1)
    0x75, 0x10, //  Report Size (16)
    0x81, 0x00, //  Input (Data,Arr,Abs)
    0xc0, // End Collection

    0x05, 0x0c, // Usage Page (Consumer Devices)
    0x09, 0x01, // Usage (Consumer Control)
    0xa1, 0x01, // Collection (Application)
    0x85, 0x04, //  Report ID (4)
    0x19, 0x01, //  Usage Minimum (1)
    0x2a, 0xa0, 0x02, //  Usage Maximum (672)
    0x15, 0x01, //  Logical Minimum (1)
    0x26, 0xa0, 0x02, //  Logical Maximum (672)
    0x95, 0x01, //  Report Count (1)
    0x75, 0x10, //  Report Size (16)
    0x81, 0x00, //  Input (Data,Arr,Abs)
    0xc0, // End Collection

    0x05, 0x01, // Usage Page (Generic Desktop)
    0x09, 0x06, // Usage (Keyboard)
    0xa1, 0x01, // Collection (Application)
    0x85, 0x06, //  Report ID (6)
    0x05, 0x07, //  Usage Page (Keyboard)
    0x19, 0xe0, //  Usage Minimum (224)
    0x29, 0xe7, //  Usage Maximum (231)
    0x15, 0x00, //  Logical Minimum (0)
    0x25, 0x01, //  Logical Maximum (1)
    0x95, 0x08, //  Report Count (8)
    0x75, 0x01, //  Report Size (1)
    0x81, 0x02, //  Input (Data,Var,Abs)
    0x05, 0x07, //  Usage Page (Keyboard)
    0x19, 0x00, //  Usage Minimum (0)
    0x29, 0xef, //  Usage Maximum (239)
    0x15, 0x00, //  Logical Minimum (0)
    0x25, 0x01, //  Logical Maximum (1)
    0x95, 0xf0, //  Report Count (240)
    0x75, 0x01, //  Report Size (1)
    0x81, 0x02, //  Input (Data,Var,Abs)
    0x05, 0x08, //  Usage Page (LEDs)
    0x19, 0x01, //  Usage Minimum (1)
    0x29, 0x05, //  Usage Maximum (5)
    0x95, 0x05, //  Report Count (5)
    0x75, 0x01, //  Report Size (1)
    0x91, 0x02, //  Output (Data,Var,Abs)
    0x95, 0x01, //  Report Count (1)
    0x75, 0x03, //  Report Size (3)
    0x91, 0x01, //  Output (Cnst,Arr,Abs)
    0xc0, // End Collection
};

#define HI_UINT16(a) (((a) >> 8) & 0xFF)
#define LO_UINT16(a) ((a) & 0xFF)
#define PROFILE_NUM            1
#define PROFILE_APP_IDX        0

struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;
    uint16_t gatts_if;
    uint16_t app_id;
    uint16_t conn_id;
};

hidd_le_env_t hidd_le_env;
uint8_t hidReportMapLen = sizeof(hidReportMap);
uint8_t hidProtocolMode = HID_PROTOCOL_MODE_REPORT;

static const uint8_t hidInfo[HID_INFORMATION_LEN] = {
    LO_UINT16(0x0111), HI_UINT16(0x0111), // bcdHID (USB HID version)
    0x00, // bCountryCode
    HID_KBD_FLAGS
};

static uint16_t hidExtReportRefDesc = ESP_GATT_UUID_BATTERY_LEVEL;

static uint8_t hidReportRefMouseIn[HID_REPORT_REF_LEN] =
        {HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT};


static uint8_t hidReportRefKeyIn[HID_REPORT_REF_LEN] =
        {HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT};

static uint8_t hidReportRefFeature[HID_REPORT_REF_LEN] =
        {HID_RPT_ID_FEATURE, HID_REPORT_TYPE_FEATURE};

static uint16_t hid_le_svc = ATT_SVC_HID;
uint16_t hid_count = 0;
esp_gatts_incl_svc_desc_t incl_svc = {0};

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t include_service_uuid = ESP_GATT_UUID_INCLUDE_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t hid_info_char_uuid = ESP_GATT_UUID_HID_INFORMATION;
static const uint16_t hid_report_map_uuid = ESP_GATT_UUID_HID_REPORT_MAP;
static const uint16_t hid_control_point_uuid = ESP_GATT_UUID_HID_CONTROL_POINT;
static const uint16_t hid_report_uuid = ESP_GATT_UUID_HID_REPORT;
static const uint16_t hid_proto_mode_uuid = ESP_GATT_UUID_HID_PROTO_MODE;
// static const uint16_t hid_kb_input_uuid = ESP_GATT_UUID_HID_BT_KB_INPUT;
// static const uint16_t hid_kb_output_uuid = ESP_GATT_UUID_HID_BT_KB_OUTPUT;
// static const uint16_t hid_mouse_input_uuid = ESP_GATT_UUID_HID_BT_MOUSE_INPUT;
static const uint16_t hid_repot_map_ext_desc_uuid = ESP_GATT_UUID_EXT_RPT_REF_DESCR;
static const uint16_t hid_report_ref_descr_uuid = ESP_GATT_UUID_RPT_REF_DESCR;

// static const uint8_t char_prop_notify = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t char_prop_read = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_write_nr = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
// static const uint8_t char_prop_read_write_notify =
// ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
// static const uint8_t char_prop_read_write_write_nr =
// ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;

/// battery Service
static const uint16_t battery_svc = ESP_GATT_UUID_BATTERY_SERVICE_SVC;

static const uint16_t bat_lev_uuid = ESP_GATT_UUID_BATTERY_LEVEL;
static const uint8_t bat_lev_ccc[2] = {0x00, 0x00};
static const uint16_t char_format_uuid = ESP_GATT_UUID_CHAR_PRESENT_FORMAT;

static uint8_t battery_lev = 95;

/// Full HRS Database Description - Used to add attributes into the database
static const esp_gatts_attr_db_t bas_att_db[BAS_IDX_NB] =
{
    // Battery Service Declaration
    [BAS_IDX_SVC] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &primary_service_uuid, ESP_GATT_PERM_READ,
            sizeof(uint16_t), sizeof(battery_svc), (uint8_t *) &battery_svc
        }
    },

    // Battery level Characteristic Declaration
    [BAS_IDX_BATT_LVL_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid, ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read_notify
        }
    },

    // Battery level Characteristic Value
    [BAS_IDX_BATT_LVL_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &bat_lev_uuid, ESP_GATT_PERM_READ,
            sizeof(uint8_t), sizeof(uint8_t), &battery_lev
        }
    },

    // Battery level Characteristic - Client Characteristic Configuration Descriptor
    [BAS_IDX_BATT_LVL_NTF_CFG] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
            sizeof(uint16_t), sizeof(bat_lev_ccc), (uint8_t *) bat_lev_ccc
        }
    },

    // Battery level report Characteristic Declaration
    [BAS_IDX_BATT_LVL_PRES_FMT] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &char_format_uuid, ESP_GATT_PERM_READ,
            sizeof(struct prf_char_pres_fmt), 0, NULL
        }
    },
};


/// Full Hid device Database Description - Used to add attributes into the database
static esp_gatts_attr_db_t hidd_le_gatt_db[HIDD_LE_IDX_NB] =
{
    // HID Service Declaration
    [HIDD_LE_IDX_SVC] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &primary_service_uuid,
            ESP_GATT_PERM_READ_ENCRYPTED, sizeof(uint16_t), sizeof(hid_le_svc),
            (uint8_t *) &hid_le_svc
        }
    },
    // HID Service Declaration
    [HIDD_LE_IDX_INCL_SVC] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &include_service_uuid,
            ESP_GATT_PERM_READ,
            sizeof(esp_gatts_incl_svc_desc_t), sizeof(esp_gatts_incl_svc_desc_t),
            (uint8_t *) &incl_svc
        }
    },
    // HID Information Characteristic Declaration
    [HIDD_LE_IDX_HID_INFO_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read
        }
    },
    // HID Information Characteristic Value
    [HIDD_LE_IDX_HID_INFO_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_info_char_uuid,
            ESP_GATT_PERM_READ_ENCRYPTED,
            sizeof(hids_hid_info_t), sizeof(hidInfo),
            (uint8_t *) &hidInfo
        }
    },
    // HID Control Point Characteristic Declaration
    [HIDD_LE_IDX_HID_CTNL_PT_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_write_nr
        }
    },
    // HID Control Point Characteristic Value
    [HIDD_LE_IDX_HID_CTNL_PT_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_control_point_uuid,
            ESP_GATT_PERM_WRITE,
            sizeof(uint8_t), 0,
            NULL
        }
    },
    // Report Map Characteristic Declaration
    [HIDD_LE_IDX_REPORT_MAP_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read
        }
    },
    // Report Map Characteristic Value
    [HIDD_LE_IDX_REPORT_MAP_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_map_uuid,
            ESP_GATT_PERM_READ_ENCRYPTED,
            HIDD_LE_REPORT_MAP_MAX_LEN, sizeof(hidReportMap),
            (uint8_t *) &hidReportMap
        }
    },
    // Report Map Characteristic - External Report Reference Descriptor
    [HIDD_LE_IDX_REPORT_MAP_EXT_REP_REF] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_repot_map_ext_desc_uuid,
            ESP_GATT_PERM_READ,
            sizeof(uint16_t), sizeof(uint16_t),
            (uint8_t *) &hidExtReportRefDesc
        }
    },
    // Protocol Mode Characteristic Declaration
    [HIDD_LE_IDX_PROTO_MODE_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read_write
        }
    },
    // Protocol Mode Characteristic Value
    [HIDD_LE_IDX_PROTO_MODE_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_proto_mode_uuid,
            (ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED),
            sizeof(uint8_t), sizeof(hidProtocolMode),
            (uint8_t *) &hidProtocolMode
        }
    },
    [HIDD_LE_IDX_REPORT_MOUSE_IN_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read_notify
        }
    },
    [HIDD_LE_IDX_REPORT_MOUSE_IN_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_uuid,
            ESP_GATT_PERM_READ_ENCRYPTED,
            HIDD_LE_REPORT_MAX_LEN, 0,
            NULL
        }
    },
    [HIDD_LE_IDX_REPORT_MOUSE_IN_CCC] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid,
            (ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED),
            sizeof(uint16_t), sizeof(uint16_t),
            (uint8_t *) &hid_ccc_default
        }
    },
    [HIDD_LE_IDX_REPORT_MOUSE_REP_REF] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_ref_descr_uuid,
            ESP_GATT_PERM_READ,
            sizeof(hidReportRefMouseIn), sizeof(hidReportRefMouseIn),
            hidReportRefMouseIn
        }
    },
    // // Report Characteristic Declaration
    // [HIDD_LE_IDX_REPORT_KEY_IN_CHAR] = {
    //     {ESP_GATT_AUTO_RSP}, {
    //         ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
    //         ESP_GATT_PERM_READ,
    //         CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
    //         (uint8_t *) &char_prop_read_notify
    //     }
    // },
    // // Report Characteristic Value
    // [HIDD_LE_IDX_REPORT_KEY_IN_VAL] = {
    //     {ESP_GATT_AUTO_RSP}, {
    //         ESP_UUID_LEN_16, (uint8_t *) &hid_report_uuid,
    //         ESP_GATT_PERM_READ_ENCRYPTED,
    //         HIDD_LE_REPORT_MAX_LEN, 0,
    //         NULL
    //     }
    // },
    // // Report KEY INPUT Characteristic - Client Characteristic Configuration Descriptor
    // [HIDD_LE_IDX_REPORT_KEY_IN_CCC] = {
    //     {ESP_GATT_AUTO_RSP}, {
    //         ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid,
    //         (ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED),
    //         sizeof(uint16_t),  sizeof(uint16_t),
    //         (uint8_t*)&hid_ccc_default
    //     }
    // },
    // // Report Characteristic - Report Reference Descriptor
    // [HIDD_LE_IDX_REPORT_KEY_IN_REP_REF] = {
    //     {ESP_GATT_AUTO_RSP}, {
    //         ESP_UUID_LEN_16, (uint8_t *) &hid_report_ref_descr_uuid,
    //         ESP_GATT_PERM_READ,
    //         sizeof(hidReportRefKeyIn), sizeof(hidReportRefKeyIn),
    //         hidReportRefKeyIn
    //     }
    // },
    //
    // Report Characteristic Declaration
    [HIDD_LE_IDX_REPORT_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read_write
        }
    },
    // Report Characteristic Value
    [HIDD_LE_IDX_REPORT_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_uuid,
            ESP_GATT_PERM_READ,
            HIDD_LE_REPORT_MAX_LEN, 0,
            NULL
        }
    },
    // Report Characteristic - Report Reference Descriptor
    [HIDD_LE_IDX_REPORT_REP_REF] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_ref_descr_uuid,
            ESP_GATT_PERM_READ,
            sizeof(hidReportRefFeature), sizeof(hidReportRefFeature),
            hidReportRefFeature
        }
    },
};

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
            ESP_LOGI(HID_LE_PRF_TAG, "HID connection establish, conn_id = %x", param->connect.conn_id);
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
        esp_ble_gatts_get_attr_value(handle, length, (const uint8_t **) value);
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

    // Key input report
    hid_rpt_map[3].id = hidReportRefKeyIn[0];
    hid_rpt_map[3].type = hidReportRefKeyIn[1];
    hid_rpt_map[3].handle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_KEY_IN_VAL];
    hid_rpt_map[3].cccdHandle = hidd_le_env.hidd_inst.att_tbl[HIDD_LE_IDX_REPORT_KEY_IN_CCC];
    hid_rpt_map[3].mode = HID_PROTOCOL_MODE_REPORT;

    // Setup report ID map
    hid_dev_register_reports(4, hid_rpt_map);
}
