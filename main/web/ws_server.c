#include "ws_server.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <cJSON.h>
#include "const.h"
#include <esp_system.h>
#include <esp_mac.h>

static const char *WS_TAG = "WS";
static httpd_handle_t server = NULL;

// Default settings JSON
static const char *default_settings = "{"
    "\"deviceInfo\":{"
        "\"name\":\"" DEVICE_NAME "\","
        "\"firmwareVersion\":\"" FIRMWARE_VERSION "\","
        "\"hardwareVersion\":\"" HARDWARE_VERSION "\","
        "\"macAddress\":\"00:00:00:00:00:00\""
    "},"
    "\"power\":{"
        "\"sleepTimeout\":30,"
        "\"lowPowerMode\":false,"
        "\"enableSleep\":true"
    "},"
    "\"led\":{"
        "\"brightness\":80"
    "},"
    "\"connectivity\":{"
        "\"bleTxPower\":\"low\","
        "\"bleReconnectDelay\":3"
    "}"
"}";

// Current settings
static char *current_settings = NULL;

#define MAX_CLIENTS CONFIG_LWIP_MAX_LISTENING_TCP
#define WS_MAX_MESSAGE_LEN 1024  // Maximum message length to prevent excessive allocations

// Static buffers for client handling
static int client_fds[CONFIG_LWIP_MAX_LISTENING_TCP];
static httpd_ws_frame_t broadcast_frame = {
    .final = true,
    .fragmented = false,
    .type = HTTPD_WS_TYPE_TEXT
};

esp_err_t ws_send_frame_to_all_clients(const char *data, const size_t len) {
    if (!server) {
        return ESP_FAIL;
    }

    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;

    const esp_err_t ret = httpd_get_client_list(server, &fds, client_fds);
    if (ret != ESP_OK) {
        return ret;
    }

    broadcast_frame.payload = (uint8_t*)data;
    broadcast_frame.len = len;

    for (int i = 0; i < fds; i++) {
        const int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            esp_err_t err = httpd_ws_send_frame_async(server, client_fds[i], &broadcast_frame);
            if (err != ESP_OK) {
                ESP_LOGW(WS_TAG, "Failed to send WS frame to client %d, error: %d", client_fds[i], err);
            }
        }
    }

    return ESP_OK;
}

void ws_broadcast_json(const char *type, const char *content) {
    if (!type || !content) return;
    
    static char buffer[WS_MAX_MESSAGE_LEN];
    const int len = snprintf(buffer, sizeof(buffer), "{\"type\":\"%s\",\"content\":%s}", type, content);
    if (len > 0 && len < sizeof(buffer)) {
        ws_send_frame_to_all_clients(buffer, len);
    }
}


// Forward declarations
extern void process_wifi_ws_message(const char* message);
static void process_settings_ws_message(const char* message);

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(WS_TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    static uint8_t frame_buffer[WS_MAX_MESSAGE_LEN];  // Fixed buffer for frame data
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
        return ret;
    }

    if (ws_pkt.len && ws_pkt.len < WS_MAX_MESSAGE_LEN) {
        ws_pkt.payload = frame_buffer;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
            return ret;
        }
        frame_buffer[ws_pkt.len] = '\0';  // Null terminate
        ESP_LOGI(WS_TAG, "Got packet with message: %s", frame_buffer);
        
        // Process the message for both settings and WiFi
        process_settings_ws_message((const char*)frame_buffer);
        process_wifi_ws_message((const char*)frame_buffer);
        
        // Echo the message back
        esp_err_t send_ret = httpd_ws_send_frame(req, &ws_pkt);
        if (send_ret != ESP_OK) {
            ESP_LOGW(WS_TAG, "Failed to echo WS frame, error: %d. Closing connection.", send_ret);
            return ESP_FAIL; // This will trigger connection closure
        }
        ret = send_ret;
    } else if (ws_pkt.len >= WS_MAX_MESSAGE_LEN) {
        ESP_LOGW(WS_TAG, "Frame too large, ignoring");
        ret = ESP_ERR_NO_MEM;
    }
    
    return ret;
}

static const httpd_uri_t ws = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = ws_handler,
    .user_ctx = NULL,
    .is_websocket = true
};

void init_websocket(httpd_handle_t server_handle) {
    server = server_handle;
    ESP_LOGI(WS_TAG, "Registering WebSocket handler");
    httpd_register_uri_handler(server, &ws);
    
    // Initialize device settings
    ESP_LOGI(WS_TAG, "Initializing device settings");
    init_device_settings();
}

void ws_queue_message(const char *data) {
    if (!data || !server) return;
    
    size_t len = strlen(data);
    if (len >= WS_MAX_MESSAGE_LEN) {
        ESP_LOGW(WS_TAG, "Message too long, truncating");
        len = WS_MAX_MESSAGE_LEN - 1;
    }
    
    // Send directly instead of queuing
    ws_send_frame_to_all_clients(data, len);
}

void ws_log(const char* text) {
    if (!text) return;
    static char buffer[WS_MAX_MESSAGE_LEN];
    const int len = snprintf(buffer, sizeof(buffer), "{\"type\":\"log\",\"content\":\"%s\"}", text);
    if (len > 0 && len < sizeof(buffer)) {
        ws_send_frame_to_all_clients(buffer, len);
    }
}

// Helper function to get MAC address as a string
static void get_mac_address_str(char *mac_str, size_t size) {
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
        ESP_LOGE(WS_TAG, "Error parsing settings JSON");
        return strdup(settings_json); // Return original if parsing fails
    }
    
    // Get the deviceInfo object
    cJSON *device_info = cJSON_GetObjectItem(root, "deviceInfo");
    if (!device_info) {
        ESP_LOGE(WS_TAG, "deviceInfo not found in settings");
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
esp_err_t init_device_settings(void) {
    // Free any existing settings
    if (current_settings != NULL) {
        free(current_settings);
        current_settings = NULL;
    }
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WS_TAG, "Error opening NVS: %s", esp_err_to_name(err));
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
            ESP_LOGE(WS_TAG, "Failed to allocate memory for settings");
            nvs_close(nvs_handle);
            // Use default settings with updated MAC address
            char *updated_settings = update_mac_address_in_settings(default_settings);
            current_settings = updated_settings ? updated_settings : strdup(default_settings);
            return ESP_ERR_NO_MEM;
        }
        
        // Get settings from NVS
        err = nvs_get_str(nvs_handle, SETTINGS_NVS_KEY, nvs_settings, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(WS_TAG, "Error getting settings from NVS: %s", esp_err_to_name(err));
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
        ESP_LOGI(WS_TAG, "No settings found in NVS, using defaults");
        char *updated_settings = update_mac_address_in_settings(default_settings);
        current_settings = updated_settings ? updated_settings : strdup(default_settings);
        
        // Save settings with updated MAC address to NVS
        if (current_settings) {
            err = nvs_set_str(nvs_handle, SETTINGS_NVS_KEY, current_settings);
            if (err != ESP_OK) {
                ESP_LOGE(WS_TAG, "Error saving settings to NVS: %s", esp_err_to_name(err));
            } else {
                err = nvs_commit(nvs_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(WS_TAG, "Error committing NVS: %s", esp_err_to_name(err));
                }
            }
        }
    }
    
    nvs_close(nvs_handle);
    return ESP_OK;
}

// Save settings to NVS
static esp_err_t save_settings_to_nvs(const char* settings_json) {
    if (!settings_json) return ESP_ERR_INVALID_ARG;
    
    // Open NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WS_TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Save settings to NVS
    err = nvs_set_str(nvs_handle, SETTINGS_NVS_KEY, settings_json);
    if (err != ESP_OK) {
        ESP_LOGE(WS_TAG, "Error saving settings to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WS_TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    return err;
}

// Process settings-related WebSocket messages
static void process_settings_ws_message(const char* message) {
    if (!message) return;
    
    // Parse JSON message
    cJSON *root = cJSON_Parse(message);
    if (!root) {
        ESP_LOGE(WS_TAG, "Error parsing JSON message");
        return;
    }
    
    // Get message type
    cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    if (!type_obj || !cJSON_IsString(type_obj)) {
        ESP_LOGE(WS_TAG, "Missing or invalid 'type' field in message");
        cJSON_Delete(root);
        return;
    }
    
    const char *type = type_obj->valuestring;
    
    // Special handling for wifi_check_saved type
    if (strcmp(type, "wifi_check_saved") == 0) {
        // This type doesn't need a command field
        cJSON_Delete(root);
        return;
    }
    
    // Get command for command type messages
    if (strcmp(type, "command") == 0) {
        cJSON *command_obj = cJSON_GetObjectItem(root, "command");
        if (!command_obj || !cJSON_IsString(command_obj)) {
            ESP_LOGE(WS_TAG, "Missing or invalid 'command' field in message");
            cJSON_Delete(root);
            return;
        }
        
        const char *command = command_obj->valuestring;
        if (strcmp(command, "get_settings") == 0) {
            // Send current settings
            if (current_settings) {
                // Make sure MAC address is up to date
                char *updated_settings = update_mac_address_in_settings(current_settings);
                if (updated_settings) {
                    free(current_settings);
                    current_settings = updated_settings;
                }
                ws_broadcast_json("settings", current_settings);
            } else {
                // Initialize settings if not already done
                init_device_settings();
                if (current_settings) {
                    ws_broadcast_json("settings", current_settings);
                }
            }
        } else if (strcmp(command, "update_settings") == 0) {
            // Update settings
            cJSON *content_obj = cJSON_GetObjectItem(root, "content");
            if (!content_obj) {
                ESP_LOGE(WS_TAG, "Missing 'content' field in update_settings command");
                cJSON_Delete(root);
                return;
            }
            
            // Convert content to string
            char *new_settings = cJSON_PrintUnformatted(content_obj);
            if (!new_settings) {
                ESP_LOGE(WS_TAG, "Error converting settings to string");
                cJSON_Delete(root);
                return;
            }
            
            // Save settings to NVS
            esp_err_t err = save_settings_to_nvs(new_settings);
            
            // Update current settings
            if (err == ESP_OK) {
                if (current_settings) {
                    free(current_settings);
                }
                current_settings = new_settings;
                
                // Send success response
                ws_broadcast_json("settings_update_status", "{\"success\":true}");
            } else {
                // Send error response
                char error_msg[100];
                snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"%s\"}", esp_err_to_name(err));
                ws_broadcast_json("settings_update_status", error_msg);
                free(new_settings);
            }
        }
    }
    
    cJSON_Delete(root);
}
