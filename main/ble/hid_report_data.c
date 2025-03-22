#include "hid_report_data.h"
#include "esp_log.h"
#include "esp_hidd_prf_api.h"
#include "hid_device_le_prf.h"

esp_gatts_incl_svc_desc_t incl_svc = {0};

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
hid_report_map_t hid_rpt_map[HID_NUM_REPORTS];

// Report reference definitions
uint8_t hidReportRefMouseIn[HID_REPORT_REF_LEN] = {HID_RPT_ID_MOUSE_IN, HID_REPORT_TYPE_INPUT};
uint8_t hidReportRefSysCtrlIn[HID_REPORT_REF_LEN] = {HID_RPT_ID_SYS_IN, HID_REPORT_TYPE_INPUT};
uint8_t hidReportRefConsumerIn[HID_REPORT_REF_LEN] = {HID_RPT_ID_CC_IN, HID_REPORT_TYPE_INPUT};
uint8_t hidReportRefKeyIn[HID_REPORT_REF_LEN] = {HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT};
uint8_t hidReportRefFeature[HID_REPORT_REF_LEN] = {HID_RPT_ID_FEATURE, HID_REPORT_TYPE_FEATURE};

static const uint16_t hid_ccc_default = 0x0100;

// HID Report Map characteristic value
const uint8_t hidReportMap[] = {
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

uint8_t hidReportMapLen = sizeof(hidReportMap);

static const uint8_t hidInfo[HID_INFORMATION_LEN] = {
    LO_UINT16(0x0111), HI_UINT16(0x0111), // bcdHID (USB HID version)
    0x00, // bCountryCode
    HID_KBD_FLAGS
};

static uint16_t hidExtReportRefDesc = ESP_GATT_UUID_BATTERY_LEVEL;
static uint16_t hid_le_svc = ATT_SVC_HID;
static const uint8_t bat_lev_ccc[2] = {0x00, 0x00};
static uint8_t battery_lev = 95;

static const uint16_t primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t include_service_uuid = ESP_GATT_UUID_INCLUDE_SERVICE;
static const uint16_t character_declaration_uuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t character_client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint16_t hid_info_char_uuid = ESP_GATT_UUID_HID_INFORMATION;
static const uint16_t hid_report_map_uuid = ESP_GATT_UUID_HID_REPORT_MAP;
static const uint16_t hid_control_point_uuid = ESP_GATT_UUID_HID_CONTROL_POINT;
static const uint16_t hid_report_uuid = ESP_GATT_UUID_HID_REPORT;
static const uint16_t hid_proto_mode_uuid = ESP_GATT_UUID_HID_PROTO_MODE;
static const uint16_t hid_repot_map_ext_desc_uuid = ESP_GATT_UUID_EXT_RPT_REF_DESCR;
static const uint16_t hid_report_ref_descr_uuid = ESP_GATT_UUID_RPT_REF_DESCR;
static const uint16_t battery_svc = ESP_GATT_UUID_BATTERY_SERVICE_SVC;
static const uint16_t bat_lev_uuid = ESP_GATT_UUID_BATTERY_LEVEL;
static const uint16_t char_format_uuid = ESP_GATT_UUID_CHAR_PRESENT_FORMAT;

static const uint8_t char_prop_read = ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_write_nr = ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t char_prop_read_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_READ;
static const uint8_t char_prop_read_notify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;

/// Full HRS Database Description - Used to add attributes into the database
const esp_gatts_attr_db_t bas_att_db[BAS_IDX_NB] =
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
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE, (uint8_t *) &char_prop_read_notify
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
esp_gatts_attr_db_t hidd_le_gatt_db[HIDD_LE_IDX_NB] =
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
    [HIDD_LE_IDX_REPORT_SYS_CTRL_IN_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read_notify
        }
    },
    [HIDD_LE_IDX_REPORT_SYS_CTRL_IN_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_uuid,
            ESP_GATT_PERM_READ_ENCRYPTED,
            HIDD_LE_REPORT_MAX_LEN, 0,
            NULL
        }
    },
    [HIDD_LE_IDX_REPORT_SYS_CTRL_IN_CCC] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid,
            (ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED),
            sizeof(uint16_t), sizeof(uint16_t),
            (uint8_t *) &hid_ccc_default
        }
    },
    [HIDD_LE_IDX_REPORT_SYS_CTRL_IN_REP_REF] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_ref_descr_uuid,
            ESP_GATT_PERM_READ,
            sizeof(hidReportRefSysCtrlIn), sizeof(hidReportRefSysCtrlIn),
            hidReportRefSysCtrlIn
        }
    },
    [HIDD_LE_IDX_REPORT_CONSUMER_IN_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read_notify
        }
    },
    [HIDD_LE_IDX_REPORT_CONSUMER_IN_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_uuid,
            ESP_GATT_PERM_READ_ENCRYPTED,
            HIDD_LE_REPORT_MAX_LEN, 0,
            NULL
        }
    },
    [HIDD_LE_IDX_REPORT_CONSUMER_IN_CCC] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid,
            (ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED),
            sizeof(uint16_t), sizeof(uint16_t),
            (uint8_t *) &hid_ccc_default
        }
    },
    [HIDD_LE_IDX_REPORT_CONSUMER_IN_REP_REF] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_ref_descr_uuid,
            ESP_GATT_PERM_READ,
            sizeof(hidReportRefConsumerIn), sizeof(hidReportRefConsumerIn),
            hidReportRefConsumerIn
        }
    },
    [HIDD_LE_IDX_REPORT_KEY_IN_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read_notify
        }
    },
    [HIDD_LE_IDX_REPORT_KEY_IN_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_uuid,
            ESP_GATT_PERM_READ_ENCRYPTED,
            HIDD_LE_REPORT_MAX_LEN, 0,
            NULL
        }
    },
    [HIDD_LE_IDX_REPORT_KEY_IN_CCC] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_client_config_uuid,
            (ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED),
            sizeof(uint16_t), sizeof(uint16_t),
            (uint8_t *) &hid_ccc_default
        }
    },
    [HIDD_LE_IDX_REPORT_KEY_IN_REP_REF] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_ref_descr_uuid,
            ESP_GATT_PERM_READ,
            sizeof(hidReportRefKeyIn), sizeof(hidReportRefKeyIn),
            hidReportRefKeyIn
        }
    },
    [HIDD_LE_IDX_REPORT_LED_OUT_CHAR] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &character_declaration_uuid,
            ESP_GATT_PERM_READ,
            CHAR_DECLARATION_SIZE, CHAR_DECLARATION_SIZE,
            (uint8_t *) &char_prop_read_write
        }
    },
    [HIDD_LE_IDX_REPORT_LED_OUT_VAL] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_uuid,
            ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED,
            HIDD_LE_REPORT_MAX_LEN, 0,
            NULL
        }
    },
    [HIDD_LE_IDX_REPORT_LED_OUT_REP_REF] = {
        {ESP_GATT_AUTO_RSP}, {
            ESP_UUID_LEN_16, (uint8_t *) &hid_report_ref_descr_uuid,
            ESP_GATT_PERM_READ,
            sizeof(hidReportRefFeature), sizeof(hidReportRefFeature),
            hidReportRefFeature
        }
    },
};
