#include "storage.h"
#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <esp_mac.h>
#include "const.h"

#define MAX_CACHE_SIZE 12
#define MAX_PATH_LENGTH 48

typedef enum {
    CACHE_TYPE_STRING,
    CACHE_TYPE_INT,
    CACHE_TYPE_BOOL
} cache_value_type_t;

typedef struct {
    char path[MAX_PATH_LENGTH];
    cache_value_type_t type;
    union {
        char str[128];
        int num;
        bool flag;
    } value;
} cache_entry_t;

static const char *STORAGE_TAG = "STORAGE";
static cache_entry_t cache[MAX_CACHE_SIZE];
static int cache_count = 0;

static void cache_clear(void) {
    cache_count = 0;
}

static cache_entry_t* cache_find(const char* path) {
    for(int i = 0; i < cache_count; i++) {
        if(strcmp(cache[i].path, path) == 0) {
            return &cache[i];
        }
    }
    return NULL;
}

static cache_entry_t* cache_add(const char* path) {
    if(cache_count >= MAX_CACHE_SIZE) {
        cache_count = 0;
    }
    strncpy(cache[cache_count].path, path, MAX_PATH_LENGTH - 1);
    cache[cache_count].path[MAX_PATH_LENGTH - 1] = '\0';
    return &cache[cache_count++];
}

// Default settings JSON
static const char *default_settings = "{"
    "\"deviceInfo\":{"
        "\"name\":\"" DEVICE_NAME "\","
        "\"firmwareVersion\":\"" FIRMWARE_VERSION "\","
        "\"hardwareVersion\":\"" HARDWARE_VERSION "\","
        "\"macAddress\":\"00:00:00:00:00:00\""
    "},"
    "\"power\":{"
        "\"lowPowerMode\":false,"
        "\"enableSleep\":true,"
        "\"highSpeedSubmode\":\"slow\","
        "\"separateSleepTimeouts\":true,"
        "\"sleepTimeout\":60,"
        "\"deepSleep\":true,"
        "\"deepSleepTimeout\":180"
    "},"
    "\"led\":{"
        "\"brightness\":25"
    "},"
    "\"mouse\":{"
        "\"sensitivity\":100"
    "},"
    "\"connectivity\":{"
        "\"bleTxPower\":\"p3\","
        "\"bleReconnectDelay\":3"
    "}"
"}";

// Current settings
static char *current_settings = NULL;

// Helper function to get MAC address as a string
static void get_mac_address_str(char *mac_str, const size_t size) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(mac_str, size, "%02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Helper function to update MAC address in settings JSON
static char* update_mac_address_in_settings(const char* settings_json) {
    if (!settings_json) return NULL;
    
    // Parse the settings JSON
    cJSON *root = cJSON_Parse(settings_json);
    if (!root) {
        ESP_LOGE(STORAGE_TAG, "Error parsing settings JSON");
        return strdup(settings_json); // Return original if parsing fails
    }
    
    // Get the deviceInfo object
    cJSON *device_info = cJSON_GetObjectItem(root, "deviceInfo");
    if (!device_info) {
        ESP_LOGE(STORAGE_TAG, "deviceInfo not found in settings");
        char *result = strdup(settings_json);
        cJSON_Delete(root);
        return result;
    }
    
    // Get the MAC address as a string
    char mac_str[18]; // XX:XX:XX:XX:XX:XX + null terminator
    get_mac_address_str(mac_str, sizeof(mac_str));
    
    // Update the MAC address
    cJSON *mac_address = cJSON_GetObjectItem(device_info, "macAddress");
    if (mac_address) {
        cJSON_SetValuestring(mac_address, mac_str);
    } else {
        cJSON_AddStringToObject(device_info, "macAddress", mac_str);
    }
    
    // Convert back to string
    char *updated_settings = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    
    return updated_settings;
}

// Initialize device settings from NVS or defaults
esp_err_t init_global_settings(void) {
    // Free any existing settings
    if (current_settings != NULL) {
        free(current_settings);
        current_settings = NULL;
    }
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(STORAGE_TAG, "Error opening NVS: %s", esp_err_to_name(err));
        // Use default settings with updated MAC address
        char *updated_settings = update_mac_address_in_settings(default_settings);
        current_settings = updated_settings ? updated_settings : strdup(default_settings);
        return err;
    }
    
    // Try to get settings from NVS
    size_t required_size = 0;
    err = nvs_get_str(nvs_handle, SETTINGS_NVS_KEY, NULL, &required_size);
    if (err == ESP_OK && required_size > 0) {
        // Allocate memory for settings
        char *nvs_settings = malloc(required_size);
        if (nvs_settings == NULL) {
            ESP_LOGE(STORAGE_TAG, "Failed to allocate memory for settings");
            nvs_close(nvs_handle);
            // Use default settings with updated MAC address
            char *updated_settings = update_mac_address_in_settings(default_settings);
            current_settings = updated_settings ? updated_settings : strdup(default_settings);
            return ESP_ERR_NO_MEM;
        }
        
        // Get settings from NVS
        err = nvs_get_str(nvs_handle, SETTINGS_NVS_KEY, nvs_settings, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(STORAGE_TAG, "Error getting settings from NVS: %s", esp_err_to_name(err));
            free(nvs_settings);
            // Use default settings with updated MAC address
            char *updated_settings = update_mac_address_in_settings(default_settings);
            current_settings = updated_settings ? updated_settings : strdup(default_settings);
        } else {
            // Update MAC address in the settings from NVS
            char *updated_settings = update_mac_address_in_settings(nvs_settings);
            current_settings = updated_settings ? updated_settings : strdup(nvs_settings);
            free(nvs_settings);
        }
    } else {
        // No settings in NVS, use defaults with updated MAC address
        ESP_LOGI(STORAGE_TAG, "No settings found in NVS, using defaults");
        char *updated_settings = update_mac_address_in_settings(default_settings);
        current_settings = updated_settings ? updated_settings : strdup(default_settings);
        
        // Save settings with updated MAC address to NVS
        if (current_settings) {
            err = nvs_set_str(nvs_handle, SETTINGS_NVS_KEY, current_settings);
            if (err != ESP_OK) {
                ESP_LOGE(STORAGE_TAG, "Error saving settings to NVS: %s", esp_err_to_name(err));
            } else {
                err = nvs_commit(nvs_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(STORAGE_TAG, "Error committing NVS: %s", esp_err_to_name(err));
                }
            }
        }
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(STORAGE_TAG, "Current settings: %s", current_settings);
    return ESP_OK;
}

// Get the current settings as a JSON string
const char* storage_get_settings(void) {
    if (current_settings == NULL) {
        // Initialize settings if not already done
        init_global_settings();
    }
    
    // Make sure MAC address is up to date
    if (current_settings) {
        char *updated_settings = update_mac_address_in_settings(current_settings);
        if (updated_settings) {
            free(current_settings);
            current_settings = updated_settings;
        }
    }
    
    return current_settings;
}

// Update device settings with new JSON
esp_err_t storage_update_settings(const char* settings_json) {
    if (!settings_json) return ESP_ERR_INVALID_ARG;
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(STORAGE_TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Save settings to NVS
    err = nvs_set_str(nvs_handle, SETTINGS_NVS_KEY, settings_json);
    if (err != ESP_OK) {
        ESP_LOGE(STORAGE_TAG, "Error saving settings to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(STORAGE_TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    
    // Update current settings and clear cache
    if (err == ESP_OK) {
        if (current_settings) {
            free(current_settings);
        }
        current_settings = strdup(settings_json);
        cache_clear();
    }
    
    return err;
}

// Helper function to find a JSON value by path
static cJSON* find_json_by_path(const char* path) {
    if (!path || !current_settings) return NULL;
    
    // Parse the settings JSON
    cJSON *root = cJSON_Parse(current_settings);
    if (!root) {
        ESP_LOGE(STORAGE_TAG, "Error parsing settings JSON");
        return NULL;
    }
    
    // Split the path by dots
    char *path_copy = strdup(path);
    if (!path_copy) {
        cJSON_Delete(root);
        return NULL;
    }
    
    char *token;
    char *rest = path_copy;
    cJSON *current = root;
    
    while ((token = strtok_r(rest, ".", &rest))) {
        current = cJSON_GetObjectItem(current, token);
        if (!current) {
            ESP_LOGE(STORAGE_TAG, "Path %s not found in settings", path);
            free(path_copy);
            cJSON_Delete(root);
            return NULL;
        }
    }
    
    free(path_copy);
    
    // Return a detached copy of the found item
    cJSON *result = cJSON_Duplicate(current, true);
    cJSON_Delete(root);
    
    return result;
}

// Get a specific setting value as a string
esp_err_t storage_get_string_setting(const char* path, char* value, const size_t max_len) {
    if (!path || !value || max_len == 0) return ESP_ERR_INVALID_ARG;
    
    cache_entry_t* entry = cache_find(path);
    if(entry && entry->type == CACHE_TYPE_STRING) {
        strncpy(value, entry->value.str, max_len - 1);
        value[max_len - 1] = '\0';
        return ESP_OK;
    }
    
    cJSON *item = find_json_by_path(path);
    if (!item) return ESP_ERR_NOT_FOUND;
    
    if (cJSON_IsString(item)) {
        entry = cache_add(path);
        entry->type = CACHE_TYPE_STRING;
        strncpy(entry->value.str, item->valuestring, sizeof(entry->value.str) - 1);
        entry->value.str[sizeof(entry->value.str) - 1] = '\0';
        
        strncpy(value, item->valuestring, max_len - 1);
        value[max_len - 1] = '\0';
        cJSON_Delete(item);
        return ESP_OK;
    }
    
    cJSON_Delete(item);
    return ESP_ERR_INVALID_ARG;
}

// Get a specific setting value as an integer
esp_err_t storage_get_int_setting(const char* path, int* value) {
    if (!path || !value) return ESP_ERR_INVALID_ARG;
    
    cache_entry_t* entry = cache_find(path);
    if(entry && entry->type == CACHE_TYPE_INT) {
        *value = entry->value.num;
        return ESP_OK;
    }
    
    cJSON *item = find_json_by_path(path);
    if (!item) return ESP_ERR_NOT_FOUND;
    
    if (cJSON_IsNumber(item)) {
        entry = cache_add(path);
        entry->type = CACHE_TYPE_INT;
        entry->value.num = item->valueint;
        
        *value = item->valueint;
        cJSON_Delete(item);
        return ESP_OK;
    }
    
    cJSON_Delete(item);
    return ESP_ERR_INVALID_ARG;
}

// Get a specific setting value as a boolean
esp_err_t storage_get_bool_setting(const char* path, bool* value) {
    if (!path || !value) return ESP_ERR_INVALID_ARG;
    
    cache_entry_t* entry = cache_find(path);
    if(entry && entry->type == CACHE_TYPE_BOOL) {
        *value = entry->value.flag;
        return ESP_OK;
    }
    
    cJSON *item = find_json_by_path(path);
    if (!item) return ESP_ERR_NOT_FOUND;
    
    if (cJSON_IsBool(item)) {
        entry = cache_add(path);
        entry->type = CACHE_TYPE_BOOL;
        entry->value.flag = cJSON_IsTrue(item);
        
        *value = cJSON_IsTrue(item);
        cJSON_Delete(item);
        return ESP_OK;
    }
    
    cJSON_Delete(item);
    return ESP_ERR_INVALID_ARG;
}

// Set the one-time boot with WiFi flag
esp_err_t storage_set_boot_with_wifi(void) {
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(WIFI_CONFIG_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(STORAGE_TAG, "Error opening NVS for boot_wifi flag: %s", esp_err_to_name(err));
        return err;
    }
    
    // Set the boot_wifi flag to 1
    err = nvs_set_u8(nvs_handle, BOOT_WIFI_KEY, 1);
    if (err != ESP_OK) {
        ESP_LOGE(STORAGE_TAG, "Error setting boot_wifi flag: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(STORAGE_TAG, "Error committing NVS for boot_wifi flag: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(STORAGE_TAG, "Successfully set boot_wifi flag");
    }
    
    nvs_close(nvs_handle);
    return err;
}

