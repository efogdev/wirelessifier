/**
 * @file connection.h
 * @brief Functions for storing and retrieving connected device information
 */

#ifndef BLE_CONN_H
#define BLE_CONN_H

#include "esp_bt_defs.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Save the current connected device address and type to NVS and cache
 *
 * @param bda Bluetooth device address to save
 * @param addr_type Address type (public or random)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t save_connected_device(esp_bd_addr_t bda, esp_ble_addr_type_t addr_type);

/**
 * @brief Load saved device data from NVS to cache
 *
 * @return esp_err_t ESP_OK if data loaded successfully, error code otherwise
 */
esp_err_t load_saved_device_to_cache(void);

/**
 * @brief Connect to the last saved device using cached data or loading from NVS
 *
 * @param gatts_if GATT server interface
 * @return esp_err_t ESP_OK if connection initiated successfully, error code otherwise
 */
esp_err_t connect_to_saved_device(esp_gatt_if_t gatts_if);

/**
 * @brief Check if there is a saved device available
 *
 * @return bool true if a saved device is available, false otherwise
 */
bool has_saved_device(void);

/**
 * @brief Get the saved device data
 *
 * @param[out] bda Pointer to store the device address
 * @param[out] addr_type Pointer to store the address type
 * @return esp_err_t ESP_OK if data retrieved successfully, error code otherwise
 */
esp_err_t get_saved_device(esp_bd_addr_t bda, esp_ble_addr_type_t *addr_type);

/**
 * @brief Clear saved device data from both cache and NVS
 *
 * @return esp_err_t ESP_OK if data cleared successfully, error code otherwise
 */
esp_err_t clear_saved_device(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_CONN_H */
