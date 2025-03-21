/*
 * SPDX-FileCopyrightText: 2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "hid_dev.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_log.h"

static hid_report_map_t *hid_dev_rpt_tbl;
static uint8_t hid_dev_rpt_tbl_Len;

#define CACHE_SIZE 8
static struct {
    uint16_t key;  // id << 8 | type
    hid_report_map_t *value;
} cache[CACHE_SIZE];
static uint8_t cache_size = 0;

static hid_report_map_t *hid_dev_rpt_by_id(uint8_t id, uint8_t type)
{
    uint16_t key = (id << 8) | type;
    
    for (uint8_t i = 0; i < cache_size; i++) {
        if (cache[i].key == key && cache[i].value->mode == hidProtocolMode) {
            return cache[i].value;
        }
    }

    hid_report_map_t *rpt = hid_dev_rpt_tbl;
    for (uint8_t i = hid_dev_rpt_tbl_Len; i > 0; i--, rpt++) {
        if (rpt->id == id && rpt->type == type && rpt->mode == hidProtocolMode) {
            if (cache_size < CACHE_SIZE) {
                cache[cache_size].key = key;
                cache[cache_size].value = rpt;
                cache_size++;
            }
            return rpt;
        }
    }

    return NULL;
}

void hid_dev_register_reports(uint8_t num_reports, hid_report_map_t *p_report)
{
    hid_dev_rpt_tbl = p_report;
    hid_dev_rpt_tbl_Len = num_reports;
    cache_size = 0;
    return;
}

void hid_keyboard_build_report(uint8_t *buffer, keyboard_cmd_t cmd)
{
    if (!buffer) {
        ESP_LOGE(HID_LE_PRF_TAG, "%s(), the buffer is NULL, hid build report failed.", __func__);
        return;
    }

    // First byte is modifier
    buffer[0] = cmd;
    // Second byte is reserved
    buffer[1] = 0;
    // Clear the key codes
    memset(&buffer[2], 0, 6);
}

void hid_dev_send_report(esp_gatt_if_t gatts_if, uint16_t conn_id,
                                    uint8_t id, uint8_t type, uint8_t length, uint8_t *data)
{
    hid_report_map_t *p_rpt;

    // get att handle for report
    if ((p_rpt = hid_dev_rpt_by_id(id, type)) != NULL) {
        // if notifications are enabled
        // ESP_LOGD(HID_LE_PRF_TAG, "%s(), send the report, handle = %d", __func__, p_rpt->handle);
        esp_ble_gatts_send_indicate(gatts_if, conn_id, p_rpt->handle, length, data, false);
    }

    return;
}
