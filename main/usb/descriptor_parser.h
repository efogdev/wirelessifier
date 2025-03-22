/**
 * @file descriptor_parser.h
 * @brief USB HID Report Descriptor Parser
 */

#pragma once

#include <stdint.h>
#include "usb_hid_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_REPORT_FIELDS 48
#define MAX_COLLECTION_DEPTH 8
#define MAX_REPORTS_PER_INTERFACE 8

typedef struct {
    usb_hid_field_attr_t attr;
    uint16_t bit_offset;
    uint16_t bit_size;
} report_field_info_t;

typedef struct {
    report_field_info_t fields[MAX_REPORT_FIELDS];
    uint8_t num_fields;
    uint16_t total_bits;
    uint16_t usage_stack[MAX_REPORT_FIELDS];
    uint8_t usage_stack_pos;
} report_info_t;

typedef struct {
    report_info_t reports[MAX_REPORTS_PER_INTERFACE];
    uint8_t report_ids[MAX_REPORTS_PER_INTERFACE];
    uint8_t num_reports;
    uint16_t collection_stack[MAX_COLLECTION_DEPTH];
    uint8_t collection_depth;
} report_map_t;

/**
 * @brief Parse a HID report descriptor
 * @param desc Report descriptor data
 * @param length Length of descriptor data
 * @param interface_num Interface number
 * @param report_map Output report map structure
 */
void parse_report_descriptor(const uint8_t *desc, size_t length, uint8_t interface_num, report_map_t *report_map);

/**
 * @brief Extract a field value from raw report data
 * @param data Raw report data
 * @param bit_offset Bit offset in the data
 * @param bit_size Size of the field in bits
 * @return Extracted field value
 */
int extract_field_value(const uint8_t *data, uint16_t bit_offset, uint16_t bit_size);

#ifdef __cplusplus
}
#endif
