#pragma once

#include <esp_err.h>
#include <stdbool.h>

// Settings namespace and keys for NVS
#define SETTINGS_NVS_NAMESPACE "device_settings"
#define SETTINGS_NVS_KEY "settings"

/**
 * @brief Initialize device settings from NVS or defaults
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t init_global_settings(void);

/**
 * @brief Get the current settings as a JSON string
 * 
 * @return const char* Current settings JSON string
 */
const char* storage_get_settings(void);

/**
 * @brief Update device settings with new JSON
 * 
 * @param settings_json New settings JSON string
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_update_settings(const char* settings_json);

/**
 * @brief Get a specific setting value as a string
 * 
 * @param path JSON path to the setting (e.g., "power.sleepTimeout")
 * @param value Buffer to store the value
 * @param max_len Maximum length of the value buffer
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_get_string_setting(const char* path, char* value, size_t max_len);

/**
 * @brief Get a specific setting value as an integer
 * 
 * @param path JSON path to the setting (e.g., "power.sleepTimeout")
 * @param value Pointer to store the integer value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_get_int_setting(const char* path, int* value);

/**
 * @brief Get a specific setting value as a boolean
 * 
 * @param path JSON path to the setting (e.g., "power.lowPowerMode")
 * @param value Pointer to store the boolean value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t storage_get_bool_setting(const char* path, bool* value);
