#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "descriptor_parser.h"
#include <usb_hid_host.h>
#include "hid_bridge.h"

static const char *TAG = "HID_DSC_PARSE";

void parse_report_descriptor(const uint8_t *desc, const size_t length, const uint8_t interface_num,
                             report_map_t *report_map) {
    uint16_t current_usage_page = 0;
    uint8_t report_size = 0;
    uint8_t report_count = 0;
    int logical_min = 0;
    int logical_max = 0;
    uint16_t current_usage = 0;
    uint16_t usage_minimum = 0;
    uint16_t usage_maximum = 0;
    bool has_usage_range = false;
    uint8_t current_report_id = 0;
    bool is_relative = false;

    report_info_t *current_report = &report_map->reports[0];
    report_map->report_ids[0] = 0;
    report_map->num_reports = 1;
    current_report->num_fields = 0;
    current_report->total_bits = 0;
    current_report->usage_stack_pos = 0;
    current_report->is_mouse = false;
    current_report->is_keyboard = false;

    for (size_t i = 0; i < length;) {
        const uint8_t item = desc[i++];
        const uint8_t item_size = item & 0x3;
        const uint8_t item_type = (item >> 2) & 0x3;
        const uint8_t item_tag = (item >> 4) & 0xF;

        uint32_t data = 0;
        if (item_size > 0) {
            for (uint8_t j = 0; j < item_size && i < length; j++) {
                data |= desc[i++] << (j * 8);
            }
        }

        switch (item_type) {
            case 0: // Main
                switch (item_tag) {
                    case 8: // Input
                    case 9: // Output
                        if (current_report && current_report->num_fields < MAX_REPORT_FIELDS) {
                            const bool is_constant = (data & 0x01) != 0;
                            const bool is_variable = (data & 0x02) != 0;
                            is_relative = (data & 0x04) != 0;
                            const bool is_array = !is_variable;

                            // If we have a report ID, find or create the corresponding report info
                            if (current_report_id != 0) {
                                int report_index = -1;
                                for (int j = 0; j < report_map->num_reports; j++) {
                                    if (report_map->report_ids[j] == current_report_id) {
                                        report_index = j;
                                        current_report = &report_map->reports[j];
                                        break;
                                    }
                                }
                                if (report_index == -1) {
                                    if (report_map->num_reports >= MAX_REPORTS_PER_INTERFACE) {
                                        ESP_LOGE(TAG, "Too many reports for interface %d", interface_num);
                                        continue;
                                    }
                                    report_index = report_map->num_reports;
                                    report_map->report_ids[report_index] = current_report_id;
                                    current_report = &report_map->reports[report_index];
                                    current_report->num_fields = 0;
                                    current_report->total_bits = 0;
                                    current_report->usage_stack_pos = 0;
                                    current_report->is_mouse = false;
                                    current_report->is_keyboard = false;
                                    report_map->num_reports++;
                                }
                            }

                            if (is_constant) {
                                // Handle constant/padding field
                                report_field_info_t *field = &current_report->fields[current_report->num_fields];
                                field->attr.usage_page = current_usage_page;
                                field->attr.usage = 0;
                                field->attr.usage_maximum = 0;
                                field->attr.report_size = report_size;
                                field->attr.report_count = report_count;
                                field->attr.logical_min = 0;
                                field->attr.logical_max = 0;
                                field->attr.constant = true;
                                field->attr.variable = false;
                                field->attr.relative = false;
                                field->attr.array = false;
                                field->bit_offset = current_report->total_bits;
                                field->bit_size = report_size * report_count;
                                current_report->total_bits += report_size * report_count;
                                current_report->num_fields++;
                            } else if (is_array) {
                                // Handle array field - store as single field with range
                                report_field_info_t *field = &current_report->fields[current_report->num_fields];
                                field->attr.usage_page = current_usage_page;
                                if (has_usage_range) {
                                    field->attr.usage = usage_minimum;
                                    field->attr.usage_maximum = usage_maximum;
                                } else if (current_report->usage_stack_pos > 0) {
                                    field->attr.usage = current_report->usage_stack[0];
                                    field->attr.usage_maximum = field->attr.usage;
                                } else {
                                    field->attr.usage = current_usage;
                                    field->attr.usage_maximum = field->attr.usage;
                                }
                                field->attr.report_size = report_size;
                                field->attr.report_count = report_count;
                                field->attr.logical_min = logical_min;
                                field->attr.logical_max = logical_max;
                                field->attr.constant = false;
                                field->attr.variable = false;
                                field->attr.relative = is_relative;
                                field->attr.array = true;
                                field->bit_offset = current_report->total_bits;
                                field->bit_size = report_size * report_count;
                                current_report->total_bits += report_size * report_count;
                                current_report->num_fields++;
                            } else {
                                // Handle variable items - FIXED SECTION
                                if (has_usage_range) {
                                    // For usage ranges, create a single field with multiple usages
                                    report_field_info_t *field = &current_report->fields[current_report->num_fields];
                                    field->attr.usage_page = current_usage_page;
                                    field->attr.usage = usage_minimum;
                                    field->attr.usage_maximum = usage_maximum;
                                    field->attr.report_size = report_size;
                                    field->attr.report_count = report_count;
                                    field->attr.logical_min = logical_min;
                                    field->attr.logical_max = logical_max;
                                    field->attr.constant = false;
                                    field->attr.variable = true;
                                    field->attr.relative = is_relative;
                                    field->attr.array = false;
                                    field->bit_offset = current_report->total_bits;
                                    field->bit_size = report_size * report_count;
                                    current_report->total_bits += report_size * report_count;
                                    current_report->num_fields++;
                                } else {
                                    // For individual usages
                                    const uint8_t usages_available = current_report->usage_stack_pos;

                                    if (usages_available == 0 && current_usage != 0) {
                                        // If we have a single usage but multiple report counts, create one field
                                        report_field_info_t *field = &current_report->fields[current_report->num_fields];
                                        field->attr.usage_page = current_usage_page;
                                        field->attr.usage = current_usage;
                                        field->attr.usage_maximum = current_usage;
                                        field->attr.report_size = report_size;
                                        field->attr.report_count = report_count;
                                        field->attr.logical_min = logical_min;
                                        field->attr.logical_max = logical_max;
                                        field->attr.constant = false;
                                        field->attr.variable = true;
                                        field->attr.relative = is_relative;
                                        field->attr.array = false;
                                        field->bit_offset = current_report->total_bits;
                                        field->bit_size = report_size * report_count;
                                        current_report->total_bits += report_size * report_count;
                                        current_report->num_fields++;
                                    } else if (usages_available >= report_count) {
                                        // If we have enough usages for each report count, create separate fields
                                        for (uint8_t j = 0; j < report_count && current_report->num_fields < MAX_REPORT_FIELDS; j++) {
                                            report_field_info_t *field = &current_report->fields[current_report->num_fields];
                                            field->attr.usage_page = current_usage_page;
                                            field->attr.usage = current_report->usage_stack[j];
                                            field->attr.usage_maximum = field->attr.usage;
                                            field->attr.report_size = report_size;
                                            field->attr.report_count = 1;
                                            field->attr.logical_min = logical_min;
                                            field->attr.logical_max = logical_max;
                                            field->attr.constant = false;
                                            field->attr.variable = true;
                                            field->attr.relative = is_relative;
                                            field->attr.array = false;
                                            field->bit_offset = current_report->total_bits;
                                            field->bit_size = report_size;
                                            current_report->total_bits += report_size;
                                            current_report->num_fields++;
                                        }
                                    } else {
                                        // If we don't have enough usages, create one field with the last usage
                                        report_field_info_t *field = &current_report->fields[current_report->num_fields];
                                        field->attr.usage_page = current_usage_page;
                                        field->attr.usage = usages_available > 0 ?
                                            current_report->usage_stack[usages_available - 1] : current_usage;
                                        field->attr.usage_maximum = field->attr.usage;
                                        field->attr.report_size = report_size;
                                        field->attr.report_count = report_count;
                                        field->attr.logical_min = logical_min;
                                        field->attr.logical_max = logical_max;
                                        field->attr.constant = false;
                                        field->attr.variable = true;
                                        field->attr.relative = is_relative;
                                        field->attr.array = false;
                                        field->bit_offset = current_report->total_bits;
                                        field->bit_size = report_size * report_count;
                                        current_report->total_bits += report_size * report_count;
                                        current_report->num_fields++;
                                    }
                                }
                            }

                            // Reset usage tracking after field processing
                            current_report->usage_stack_pos = 0;
                            has_usage_range = false;
                            usage_minimum = 0;
                            usage_maximum = 0;
                        }
                        break;
                    case 10: // Collection
                        if (report_map->collection_depth < MAX_COLLECTION_DEPTH) {
                            report_map->collection_stack[report_map->collection_depth++] = data;
                        }
                        break;
                    case 12: // End Collection
                        if (report_map->collection_depth > 0) {
                            report_map->collection_depth--;
                        }
                        break;
                }
                break;

            case 1: // Global
                switch (item_tag) {
                    case 0: // Usage Page
                        current_usage_page = data;
                        break;
                    case 1: // Logical Minimum
                        if (item_size == 1 && (data & 0x80)) {
                            logical_min = (int8_t) data;
                        } else if (item_size == 2 && (data & 0x8000)) {
                            logical_min = (int16_t) data;
                        } else {
                            logical_min = (int) data;
                        }
                        break;
                    case 2: // Logical Maximum
                        if (item_size == 1 && (data & 0x80)) {
                            logical_max = (int8_t) data;
                        } else if (item_size == 2 && (data & 0x8000)) {
                            logical_max = (int16_t) data;
                        } else {
                            logical_max = (int) data;
                        }
                        break;
                    case 7: // Report Size
                        report_size = data;
                        break;
                    case 8: // Report ID
                        current_report_id = data;
                        break;
                    case 9: // Report Count
                        report_count = data;
                        break;
                }
                break;

            case 2: // Local
                switch (item_tag) {
                    case 0: // Usage
                        if (current_report && current_report->usage_stack_pos < MAX_REPORT_FIELDS) {
                            current_report->usage_stack[current_report->usage_stack_pos++] = data;
                        }
                        current_usage = data;
                        break;
                    case 1: // Usage Minimum
                        usage_minimum = data;
                        has_usage_range = true;
                        break;
                    case 2: // Usage Maximum
                        usage_maximum = data;
                        has_usage_range = true;
                        break;
                }
                break;
        }
    }

    // ESP_LOGI(TAG, "=== Report Descriptor Summary ===");
    // ESP_LOGI(TAG, "Interface: %d, Total Reports: %d", interface_num, report_map->num_reports);
    //
    // for (int i = 0; i < report_map->num_reports; i++) {
    //     report_info_t *report = &report_map->reports[i];
    //     ESP_LOGI(TAG, "\nReport %d (ID: 0x%02x):", i, report_map->report_ids[i]);
    //     ESP_LOGI(TAG, "Total Fields: %d, Total Bits: %d", report->num_fields, report->total_bits);
    //
    //     for (int j = 0; j < report->num_fields; j++) {
    //         const report_field_info_t *field = &report->fields[j];
    //         ESP_LOGI(TAG, "  Field %d:", j);
    //         ESP_LOGI(TAG, "    Usage Page: 0x%04x", field->attr.usage_page);
    //         ESP_LOGI(TAG, "    Usage: 0x%04x-0x%04x", field->attr.usage, field->attr.usage_maximum);
    //         ESP_LOGI(TAG, "    Report Size: %d bits", field->attr.report_size);
    //         ESP_LOGI(TAG, "    Report Count: %d", field->attr.report_count);
    //         ESP_LOGI(TAG, "    Logical Range: %d to %d", field->attr.logical_min, field->attr.logical_max);
    //         ESP_LOGI(TAG, "    Attributes: %s%s%s%s",
    //             field->attr.constant ? "Constant " : "",
    //             field->attr.variable ? "Variable " : "",
    //             field->attr.relative ? "Relative " : "",
    //             field->attr.array ? "Array" : "");
    //         ESP_LOGI(TAG, "    Bit Offset: %d, Size: %d", field->bit_offset, field->bit_size);
    //     }
    // }
    // ESP_LOGI(TAG, "==============================");

    for (int i = 0; i < report_map->num_reports; i++) {
        report_info_t *report = &report_map->reports[i];
        for (int j = 0; j < report->num_fields; j++) {
            const report_field_info_t *field = &report->fields[j];

            if (field->attr.usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP) {
                if (field->attr.usage == HID_USAGE_X || field->attr.usage == HID_USAGE_Y) {
                    report->is_mouse = true;
                }
                if (field->attr.usage == HID_USAGE_X) {
                    report->mouse_fields.x = j;
                } else if (field->attr.usage == HID_USAGE_Y) {
                    report->mouse_fields.y = j;
                } else if (field->attr.usage == HID_USAGE_WHEEL) {
                    report->mouse_fields.wheel = j;
                }
            } else if (field->attr.usage == HID_USAGE_PAGE_GENERIC_DESKTOP && field->attr.usage_page == HID_USAGE_PAGE_BUTTON) {
                report->mouse_fields.buttons = j;
            } else if (field->attr.usage == 0x238) { // Pan
                report->mouse_fields.pan = j;
            }

            if (field->attr.usage_page == HID_USAGE_KEYPAD) {
                report->is_keyboard = true;
            }
        }

        if (report->is_keyboard) {
            report->is_mouse = false;
        }
    }
}

IRAM_ATTR int64_t extract_field_value(const uint8_t *data, const uint16_t bit_offset,
                                                                    const uint16_t bit_size) {
    if (!data || bit_size == 0 || bit_size > 64) {
        return 0;
    }

    uint64_t value = 0;
    uint16_t byte_offset = bit_offset / 8;
    uint8_t bit_shift = bit_offset % 8;
    uint16_t bits_remaining = bit_size;
    uint16_t current_bit = 0;

    // Handle simple case for single bit
    if (bit_size == 1) {
        const uint8_t byte_value = data[byte_offset];
        return (byte_value >> bit_shift) & 0x01;
    }

    // Extract bits from each byte
    while (bits_remaining > 0) {
        // How many bits we can read from current byte
        const uint8_t bits_to_read = MIN(8 - bit_shift, bits_remaining);

        // Extract bits from current byte
        const uint8_t mask = (1 << bits_to_read) - 1;
        const uint8_t byte_value = (data[byte_offset] >> bit_shift) & mask;

        // Add these bits to our result
        value |= ((uint64_t)byte_value << current_bit);

        // Update counters
        current_bit += bits_to_read;
        bits_remaining -= bits_to_read;
        byte_offset++;
        bit_shift = 0;  // After first byte, we always start at bit 0
    }

    // Handle sign extension for signed values
    if (bit_size < 64) {
        // Check if the highest bit is set (negative value)
        if (value & (1ULL << (bit_size - 1))) {
            // Sign extend by setting all higher bits to 1
            value |= ~((1ULL << bit_size) - 1);
        }
    }

    return (int64_t)value;
}
