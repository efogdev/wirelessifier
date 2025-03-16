#include "wifi_manager.h"
#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ws_server.h"
#include "http_server.h"

static const char *WIFI_TAG = "WIFI_MGR";

// NVS keys
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"

int s_retry_num = 0;
static bool connecting = false;

// WiFi connection status
static bool is_connected = false;
static char connected_ip[16] = {0};

// Connect to WiFi with stored credentials
esp_err_t connect_wifi_with_stored_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Read SSID
    size_t ssid_len = 32; // Max SSID length
    char ssid[33] = {0};  // +1 for null terminator
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "No stored SSID found: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    // Read password
    size_t pass_len = 64; // Max password length
    char password[65] = {0}; // +1 for null terminator
    err = nvs_get_str(nvs_handle, NVS_KEY_PASS, password, &pass_len);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "No stored password found: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    ESP_LOGI(WIFI_TAG, "Connecting to %s...", ssid);
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Use esp_err_t to handle the return value instead of ESP_ERROR_CHECK
    esp_err_t connect_err = esp_wifi_connect();
    if (connect_err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Failed to connect to WiFi: %s", esp_err_to_name(connect_err));
        return connect_err;
    }
    
    // Wait for connection or failure
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(WIFI_TAG, "Connected to %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(WIFI_TAG, "Failed to connect to %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(WIFI_TAG, "Unexpected event");
        return ESP_ERR_INVALID_STATE;
    }
}

// Save WiFi credentials to NVS
esp_err_t save_wifi_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error saving SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error saving password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

// Clear WiFi credentials from NVS
esp_err_t clear_wifi_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(nvs_handle, NVS_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_erase_key(nvs_handle, NVS_KEY_PASS);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

// Check if WiFi credentials are stored
bool has_wifi_credentials(void) {
    if (connecting) {
        return true;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t ssid_len = 0;
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, NULL, &ssid_len);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK && ssid_len > 0);
}

// Scan for available WiFi networks
esp_err_t scan_wifi_networks(void) {
    ESP_LOGI(WIFI_TAG, "Starting WiFi scan...");
    
    // Configure scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300
    };
    
    // Start scan
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    
    // Get scan results
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    
    if (ap_count == 0) {
        ESP_LOGI(WIFI_TAG, "No networks found");
        ws_broadcast_json("wifi_scan_result", "[]");
        return ESP_OK;
    }
    
    // Allocate memory for results
    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_records == NULL) {
        ESP_LOGE(WIFI_TAG, "Failed to allocate memory for scan results");
        return ESP_ERR_NO_MEM;
    }
    
    // Get scan results
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records));
    
    // Build JSON array of results
    char *json_buffer = malloc(ap_count * 128); // Allocate enough space for all APs
    if (json_buffer == NULL) {
        free(ap_records);
        ESP_LOGE(WIFI_TAG, "Failed to allocate memory for JSON buffer");
        return ESP_ERR_NO_MEM;
    }
    
    int offset = 0;
    offset += sprintf(json_buffer + offset, "[");
    
    for (int i = 0; i < ap_count; i++) {
        char ssid_escaped[64] = {0};
        
        // Simple JSON escaping for SSID
        for (int j = 0, k = 0; j < strlen((char*)ap_records[i].ssid) && k < 63; j++) {
            if (ap_records[i].ssid[j] == '"' || ap_records[i].ssid[j] == '\\') {
                ssid_escaped[k++] = '\\';
            }
            ssid_escaped[k++] = ap_records[i].ssid[j];
        }
        
        offset += sprintf(json_buffer + offset, 
                         "%s{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":%d}",
                         i > 0 ? "," : "",
                         ssid_escaped,
                         ap_records[i].rssi,
                         ap_records[i].authmode);
    }
    
    offset += sprintf(json_buffer + offset, "]");
    
    // Send results via WebSocket
    ws_broadcast_json("wifi_scan_result", json_buffer);
    
    // Clean up
    free(json_buffer);
    free(ap_records);
    
    return ESP_OK;
}

// Connect to a specific WiFi network
esp_err_t connect_to_wifi(const char* ssid, const char* password) {
    if (!ssid || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (connecting) {
        return ESP_OK;
    }

    connecting = true;
    s_retry_num = 0;
    if (is_connected) {
        esp_wifi_disconnect();
    }
    
    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strlcpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    
    if (password) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        strlcpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    }
    
    ESP_LOGI(WIFI_TAG, "Connecting to %s...", ssid);

    // Set WiFi configuration and start
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    // Use esp_err_t to handle the return value instead of ESP_ERROR_CHECK
    esp_err_t connect_err = esp_wifi_connect();
    if (connect_err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Failed to connect to WiFi: %s", esp_err_to_name(connect_err));
        return connect_err;
    }
    
    // Wait for connection or failure
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdTRUE, 40000 / portTICK_PERIOD_MS);
    
    connecting = false;

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(WIFI_TAG, "Connected to %s", ssid);
        
        // Save credentials if connection successful
        save_wifi_credentials(ssid, password ? password : "");
        
        // Send success status
        char status_json[128];
        snprintf(status_json, sizeof(status_json), 
                "{\"connected\":true,\"ip\":\"%s\",\"attempt\":%d}", 
                connected_ip, s_retry_num);
        ws_broadcast_json("wifi_connect_status", status_json);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();

        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(WIFI_TAG, "Failed to connect to %s", ssid);
        
        // Send failure status
        char status_json[64];
        snprintf(status_json, sizeof(status_json), 
                "{\"connected\":false,\"attempt\":%d}", 
                s_retry_num);
        ws_broadcast_json("wifi_connect_status", status_json);
        
        return ESP_FAIL;
    } else {
        ESP_LOGE(WIFI_TAG, "Connection timeout");
        
        // Send timeout status
        char status_json[64];
        snprintf(status_json, sizeof(status_json), 
                "{\"connected\":false,\"attempt\":%d}", 
                s_retry_num);
        ws_broadcast_json("wifi_connect_status", status_json);
        
        return ESP_ERR_TIMEOUT;
    }
}

// Process WebSocket messages for WiFi management
void process_wifi_ws_message(const char* message) {
    // Simple JSON parsing - in a real application, use a proper JSON parser
    if (strstr(message, "\"type\":\"wifi_scan\"")) {
        scan_wifi_networks();
    } 
    else if (strstr(message, "\"type\":\"wifi_connect\"")) {
        // Extract SSID and password
        char ssid[33] = {0};
        char password[65] = {0};
        
        // Very basic extraction - a real app should use proper JSON parsing
        const char* ssid_start = strstr(message, "\"ssid\":\"");
        const char* pass_start = strstr(message, "\"password\":\"");
        
        if (ssid_start) {
            ssid_start += 8; // Skip "ssid":"
            const char* ssid_end = strchr(ssid_start, '"');
            if (ssid_end && (ssid_end - ssid_start < 33)) {
                memcpy(ssid, ssid_start, ssid_end - ssid_start);
            }
        }
        
        if (pass_start) {
            pass_start += 12; // Skip "password":"
            const char* pass_end = strchr(pass_start, '"');
            if (pass_end && (pass_end - pass_start < 65)) {
                memcpy(password, pass_start, pass_end - pass_start);
            }
        }
        
        if (strlen(ssid) > 0) {
            connect_to_wifi(ssid, password);
        }
    }
}

// Update connection status based on events
void update_wifi_connection_status(bool connected, const char* ip) {
    is_connected = connected;
    if (ip) {
        strlcpy(connected_ip, ip, sizeof(connected_ip));
    }
}

// Check if device is connected to WiFi
bool is_wifi_connected(void) {
    return is_connected;
}
