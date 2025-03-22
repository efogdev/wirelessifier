/*
 * SPDX-FileCopyrightText: 2021-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

#include <hid_dev.h>

#include "esp_gatts_api.h"
#include "esp_hidd_prf_api.h"

#define CHAR_DECLARATION_SIZE   (sizeof(uint8_t))
#define HI_UINT16(a) (((a) >> 8) & 0xFF)
#define LO_UINT16(a) ((a) & 0xFF)
#define PROFILE_NUM            1
#define PROFILE_APP_IDX        0

extern esp_gatts_incl_svc_desc_t incl_svc;

// HID report mapping table
extern hid_report_map_t hid_rpt_map[HID_NUM_REPORTS];

// HID Report Map characteristic value
extern const uint8_t hidReportMap[];
extern uint8_t hidReportMapLen;

extern uint8_t hidReportRefMouseIn[HID_REPORT_REF_LEN];
extern uint8_t hidReportRefSysCtrlIn[HID_REPORT_REF_LEN];
extern uint8_t hidReportRefConsumerIn[HID_REPORT_REF_LEN];
extern uint8_t hidReportRefKeyIn[HID_REPORT_REF_LEN];
extern uint8_t hidReportRefFeature[HID_REPORT_REF_LEN];

// Battery Service Attributes Indexes
enum {
    BAS_IDX_SVC,
    BAS_IDX_BATT_LVL_CHAR,
    BAS_IDX_BATT_LVL_VAL,
    BAS_IDX_BATT_LVL_NTF_CFG,
    BAS_IDX_BATT_LVL_PRES_FMT,
    BAS_IDX_NB,
};

/// Full HRS Database Description - Used to add attributes into the database
extern const esp_gatts_attr_db_t bas_att_db[BAS_IDX_NB];

/// Full Hid device Database Description - Used to add attributes into the database
extern esp_gatts_attr_db_t hidd_le_gatt_db[HIDD_LE_IDX_NB];
