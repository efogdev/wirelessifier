#pragma once

#include <stdint.h>
#include <usb/hid_host.h>
#include "hid_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize descriptor parser and load cache from NVS
 */
void descriptor_parser_init(void);

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
int64_t extract_field_value(const uint8_t *data, uint16_t bit_offset, uint16_t bit_size);

#ifdef __cplusplus
}
#endif
