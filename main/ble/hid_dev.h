#ifndef HID_DEV_H__
#define HID_DEV_H__

#include "hid_device_le_prf.h"
#include "hid_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((packed))
{
  uint16_t    handle;           // Handle of report characteristic
  uint16_t    cccdHandle;       // Handle of CCCD for report characteristic
  uint8_t     id;               // Report ID
  uint8_t     type;             // Report type
  uint8_t     mode;             // Protocol mode (report or boot)
} hid_report_map_t;

typedef struct __attribute__((packed))
{
  uint32_t    idleTimeout;      // Idle timeout in milliseconds
  uint8_t     hidFlags;         // HID feature flags
} hid_dev_cfg_t;

void hid_dev_register_reports(uint8_t num_reports, hid_report_map_t *p_report);

void hid_dev_send_report(esp_gatt_if_t gatts_if, uint16_t conn_id,
                        uint8_t id, uint8_t type, uint8_t length, uint8_t *data);

void hid_keyboard_build_report(uint8_t *buffer, keyboard_cmd_t cmd);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* HID_DEV_H__ */
