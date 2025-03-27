#include "hid_dev.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "esp_log.h"

static hid_report_map_t *hid_dev_rpt_tbl;
static uint8_t hid_dev_rpt_tbl_Len;

#define CACHE_SIZE 8
#define DIRECT_CACHE_SIZE 4

typedef struct {
    uint16_t key; // id << 8 | type
    uint16_t _pad;  // 32-bit alignment
    hid_report_map_t *value;
} cache_entry_t;

static cache_entry_t __attribute__((section(".dram1.data"))) cache[CACHE_SIZE];
static hid_report_map_t * __attribute__((section(".dram1.data"))) direct_cache[DIRECT_CACHE_SIZE];
static uint8_t __attribute__((section(".dram1.data"))) cache_size = 0;

static uint8_t s_report_buffer[48] __attribute__((section(".dram1.data")));

static inline hid_report_map_t *hid_dev_rpt_by_id(const uint8_t id, const uint8_t type) {
    // Direct indexing for most common reports (assuming IDs 0-3 are most frequent)
    if (id < DIRECT_CACHE_SIZE && direct_cache[id] && 
        direct_cache[id]->id == id && 
        direct_cache[id]->type == type) {
        return direct_cache[id];
    }

    const uint16_t key = (id << 8) | type;
    
    // Unrolled cache lookup for better branch prediction
    if (cache_size > 0 && cache[0].key == key) return cache[0].value;
    if (cache_size > 1 && cache[1].key == key) return cache[1].value;
    if (cache_size > 2 && cache[2].key == key) return cache[2].value;
    if (cache_size > 3 && cache[3].key == key) return cache[3].value;

    hid_report_map_t *rpt = hid_dev_rpt_tbl;
    for (uint8_t i = hid_dev_rpt_tbl_Len; i > 0; i--, rpt++) {
        if (rpt->id == id && rpt->type == type) {
            // Update direct cache for frequent IDs
            if (id < DIRECT_CACHE_SIZE) {
                direct_cache[id] = rpt;
            }
            // Update LRU cache
            if (cache_size < CACHE_SIZE) {
                cache[cache_size].key = key;
                cache[cache_size].value = rpt;
                cache_size++;
            } else {
                // Move everything down and put new entry at front
                memmove(&cache[1], &cache[0], sizeof(cache_entry_t) * (CACHE_SIZE - 1));
                cache[0].key = key;
                cache[0].value = rpt;
            }
            return rpt;
        }
    }

    return NULL;
}

void hid_dev_register_reports(const uint8_t num_reports, hid_report_map_t *p_report) {
    hid_dev_rpt_tbl = p_report;
    hid_dev_rpt_tbl_Len = num_reports;
    cache_size = 0;
    memset(direct_cache, 0, sizeof(direct_cache));
}

void hid_keyboard_build_report(uint8_t *buffer, const keyboard_cmd_t cmd) {
    if (!buffer) {
        ESP_LOGE(HID_LE_PRF_TAG, "%s(), the buffer is NULL, hid build report failed.", __func__);
        return;
    }

    buffer[0] = cmd;
    buffer[1] = 0;
    memset(&buffer[2], 0, 6);
}

__attribute__((section(".iram1.text"))) void hid_dev_send_report(const esp_gatt_if_t gatts_if, const uint16_t conn_id,
                         const uint8_t id, const uint8_t type, const uint8_t length, const uint8_t *data) {
    hid_report_map_t *p_rpt;
    if ((p_rpt = hid_dev_rpt_by_id(id, type)) != NULL) {
        memcpy(s_report_buffer, data, length);
        esp_ble_gatts_send_indicate(gatts_if, conn_id, p_rpt->handle, length, s_report_buffer, false);
    }
}
