#include "connection.h"
#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/projdefs.h>
#include <freertos/task.h>

#include "ble_hid_device.h"
#include "const.h"
#include "nvs.h"

#define STORAGE_NAMESPACE "hid_dev"
#define ADDR_KEY "last_addr"
#define ADDR_TYPE_KEY "addr_type"

static const char *TAG = "BLE_CONN";

static struct {
    esp_bd_addr_t bda;
    esp_ble_addr_type_t addr_type;
    bool is_valid;
} saved_device_cache = {
    .is_valid = false
};

/**
 * @brief Save the current connected device address and type to NVS and cache
 *
 * @param bda Bluetooth device address to save
 * @param addr_type Address type (public or random)
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t save_connected_device(esp_bd_addr_t bda, const esp_ble_addr_type_t addr_type) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    memcpy(saved_device_cache.bda, bda, ESP_BD_ADDR_LEN);
    saved_device_cache.addr_type = addr_type;
    saved_device_cache.is_valid = true;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, ADDR_KEY, bda, ESP_BD_ADDR_LEN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving device address: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_u8(nvs_handle, ADDR_TYPE_KEY, (uint8_t)addr_type);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error saving address type: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    if (VERBOSE) {
        ESP_LOGI(TAG, "Saved device: %02x:%02x:%02x:%02x:%02x:%02x, type: %d",
                 bda[0], bda[1], bda[2], bda[3], bda[4], bda[5], addr_type);
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}

/**
 * @brief Load saved device data from NVS to cache
 *
 * @return esp_err_t ESP_OK if data loaded successfully, error code otherwise
 */
esp_err_t load_saved_device_to_cache(void) {
    if (saved_device_cache.is_valid) {
        return ESP_OK;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err;
    size_t addr_size = ESP_BD_ADDR_LEN;
    uint8_t addr_type_val;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_get_blob(nvs_handle, ADDR_KEY, saved_device_cache.bda, &addr_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading device address: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_u8(nvs_handle, ADDR_TYPE_KEY, &addr_type_val);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading address type: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);
    saved_device_cache.addr_type = (esp_ble_addr_type_t)addr_type_val;

    bool valid_addr = false;
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++) {
        if (saved_device_cache.bda[i] != 0) {
            valid_addr = true;
            break;
        }
    }

    saved_device_cache.is_valid = valid_addr;

    if (valid_addr) {
        if (VERBOSE) {
            ESP_LOGI(TAG, "Loaded device to cache: %02x:%02x:%02x:%02x:%02x:%02x, type: %d",
                     saved_device_cache.bda[0], saved_device_cache.bda[1], saved_device_cache.bda[2],
                     saved_device_cache.bda[3], saved_device_cache.bda[4], saved_device_cache.bda[5],
                     saved_device_cache.addr_type);
        }

        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "No valid device found in storage");
        return ESP_ERR_NOT_FOUND;
    }
}

/**
 * @brief Connect to the last saved device using cached data or loading from NVS
 * 
 * @param gatts_if GATT server interface
 * @return esp_err_t ESP_OK if connection initiated successfully, error code otherwise
 */
esp_err_t connect_to_saved_device(const esp_gatt_if_t gatts_if) {
    esp_err_t err;
    if (!saved_device_cache.is_valid) {
        err = load_saved_device_to_cache();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to load saved device data");
            return err;
        }
    }

    if (!saved_device_cache.is_valid) {
        return ESP_OK;
    }

    if (VERBOSE) {
        ESP_LOGI(TAG, "Connecting to saved device: %02x:%02x:%02x:%02x:%02x:%02x, type: %d",
                 saved_device_cache.bda[0], saved_device_cache.bda[1], saved_device_cache.bda[2],
                 saved_device_cache.bda[3], saved_device_cache.bda[4], saved_device_cache.bda[5],
                 saved_device_cache.addr_type);
    }

    ble_hid_device_start_advertising();

    err = esp_ble_gatts_open(gatts_if, saved_device_cache.bda, true); // true for direct connection
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to saved device, error: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

/**
 * @brief Check if there is a saved device available
 *
 * @return bool true if a saved device is available, false otherwise
 */
bool has_saved_device(void) {
    // If cache is not valid, try to load from NVS
    if (!saved_device_cache.is_valid) {
        if (load_saved_device_to_cache() != ESP_OK) {
            return false;
        }
    }

    return saved_device_cache.is_valid;
}

/**
 * @brief Get the saved device data
 *
 * @param[out] bda Pointer to store the device address
 * @param[out] addr_type Pointer to store the address type
 * @return esp_err_t ESP_OK if data retrieved successfully, error code otherwise
 */
esp_err_t get_saved_device(esp_bd_addr_t bda, esp_ble_addr_type_t *addr_type) {
    if (!saved_device_cache.is_valid) {
        const esp_err_t err = load_saved_device_to_cache();
        if (err != ESP_OK) {
            return err;
        }
    }

    if (!saved_device_cache.is_valid) {
        return ESP_ERR_NOT_FOUND;
    }

    if (bda != NULL) {
        memcpy(bda, saved_device_cache.bda, ESP_BD_ADDR_LEN);
    }

    if (addr_type != NULL) {
        *addr_type = saved_device_cache.addr_type;
    }

    return ESP_OK;
}

/**
 * @brief Clear saved device data from both cache and NVS
 *
 * @return esp_err_t ESP_OK if data cleared successfully, error code otherwise
 */
esp_err_t clear_saved_device(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    memset(saved_device_cache.bda, 0, ESP_BD_ADDR_LEN);
    saved_device_cache.addr_type = BLE_ADDR_TYPE_PUBLIC;
    saved_device_cache.is_valid = false;

    err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_erase_key(nvs_handle, ADDR_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Error erasing device address: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_erase_key(nvs_handle, ADDR_TYPE_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Error erasing address type: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    if (VERBOSE) {
        ESP_LOGI(TAG, "Cleared saved device data");
    }

    nvs_close(nvs_handle);
    return ESP_OK;
}
