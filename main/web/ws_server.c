#include "ws_server.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include <cJSON.h>
#include "../utils/storage.h"

static const char *WS_TAG = "WS";
static httpd_handle_t server = NULL;

#define MAX_CLIENTS CONFIG_LWIP_MAX_LISTENING_TCP
#define WS_MAX_MESSAGE_LEN 1024  // Maximum message length to prevent excessive allocations

// Static buffers for client handling
static int client_fds[CONFIG_LWIP_MAX_LISTENING_TCP];
static httpd_ws_frame_t broadcast_frame = {
    .final = true,
    .fragmented = false,
    .type = HTTPD_WS_TYPE_TEXT
};

// Track clients that have failed to receive messages
static int failed_clients[CONFIG_LWIP_MAX_LISTENING_TCP];
static int failed_clients_count = 0;

// Helper function to check if a client is in the failed list
static bool is_failed_client(int fd) {
    for (int i = 0; i < failed_clients_count; i++) {
        if (failed_clients[i] == fd) {
            return true;
        }
    }
    return false;
}

// Helper function to add a client to the failed list
static void add_failed_client(int fd) {
    // Check if already in the list
    if (is_failed_client(fd)) {
        return;
    }
    
    // Add to the list if there's space
    if (failed_clients_count < CONFIG_LWIP_MAX_LISTENING_TCP) {
        failed_clients[failed_clients_count++] = fd;
    }
}

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
        // Skip clients that have previously failed
        if (is_failed_client(client_fds[i])) {
            continue;
        }
        
        const int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            esp_err_t err = httpd_ws_send_frame_async(server, client_fds[i], &broadcast_frame);
            if (err != ESP_OK) {
                ESP_LOGW(WS_TAG, "Failed to send WS frame to client %d, error: %d", client_fds[i], err);
                
                // Close the connection for any send error to clean up resources
                httpd_sess_trigger_close(server, client_fds[i]);
                ESP_LOGI(WS_TAG, "Closed connection to client %d after send error", client_fds[i]);
                
                // Add to failed clients list to avoid future send attempts
                add_failed_client(client_fds[i]);
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

// Helper function to remove a client from the failed list
static void remove_failed_client(int fd) {
    for (int i = 0; i < failed_clients_count; i++) {
        if (failed_clients[i] == fd) {
            // Move the last element to this position (if not already the last)
            if (i < failed_clients_count - 1) {
                failed_clients[i] = failed_clients[failed_clients_count - 1];
            }
            failed_clients_count--;
            break;
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(WS_TAG, "Handshake done, the new connection was opened");
        
        // Register the socket with the server
        int sockfd = httpd_req_to_sockfd(req);
        if (sockfd != -1) {
            ESP_LOGI(WS_TAG, "New WebSocket client connected: %d", sockfd);
            
            // Remove this socket from failed clients list if it was there
            // (in case the same file descriptor is reused for a new connection)
            remove_failed_client(sockfd);
        }
        
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    static uint8_t frame_buffer[WS_MAX_MESSAGE_LEN];  // Fixed buffer for frame data
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
        if (ret == ESP_ERR_INVALID_STATE || ret == HTTPD_SOCK_ERR_FAIL) {
            // Client likely disconnected
            int sockfd = httpd_req_to_sockfd(req);
            ESP_LOGI(WS_TAG, "Client %d appears to be disconnected, closing session", sockfd);
            
            // Add to failed clients list to avoid future send attempts
            if (sockfd != -1) {
                add_failed_client(sockfd);
            }
            
            return ESP_FAIL; // This will trigger connection closure
        }
        return ret;
    }

    if (ws_pkt.len && ws_pkt.len < WS_MAX_MESSAGE_LEN) {
        ws_pkt.payload = frame_buffer;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
            if (ret == ESP_ERR_INVALID_STATE || ret == HTTPD_SOCK_ERR_FAIL) {
                // Client likely disconnected
                int sockfd = httpd_req_to_sockfd(req);
                ESP_LOGI(WS_TAG, "Client %d appears to be disconnected during frame receive, closing session", sockfd);
                
                // Add to failed clients list to avoid future send attempts
                if (sockfd != -1) {
                    add_failed_client(sockfd);
                }
                
                return ESP_FAIL; // This will trigger connection closure
            }
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
            
            // Add to failed clients list to avoid future send attempts
            int sockfd = httpd_req_to_sockfd(req);
            if (sockfd != -1) {
                add_failed_client(sockfd);
            }
            
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
    
    // Initialize client tracking
    failed_clients_count = 0;
    
    // Initialize device settings
    ESP_LOGI(WS_TAG, "Initializing device settings");
    init_global_settings();
}

void ws_queue_message(const char *data) {
    if (!data || !server) return;
    
    size_t len = strlen(data);
    if (len >= WS_MAX_MESSAGE_LEN) {
        ESP_LOGW(WS_TAG, "Message too long, truncating");
        len = WS_MAX_MESSAGE_LEN - 1;
    }
    
    // Send directly instead of queuing
    esp_err_t err = ws_send_frame_to_all_clients(data, len);
    if (err != ESP_OK) {
        ESP_LOGW(WS_TAG, "Failed to send message to clients: %s", esp_err_to_name(err));
    }
}

void ws_log(const char* text) {
    if (!text) return;
    static char buffer[WS_MAX_MESSAGE_LEN];
    const int len = snprintf(buffer, sizeof(buffer), "{\"type\":\"log\",\"content\":\"%s\"}", text);
    if (len > 0 && len < sizeof(buffer)) {
        ws_send_frame_to_all_clients(buffer, len);
    }
}

// Initialize device settings from NVS or defaults
esp_err_t init_device_settings(void) {
    // Use the storage module to initialize settings
    return init_global_settings();
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
            const char* settings = storage_get_settings();
            if (settings) {
                ws_broadcast_json("settings", settings);
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
            
            // Save settings using the storage module
            esp_err_t err = storage_update_settings(new_settings);
            
            // Send response based on result
            if (err == ESP_OK) {
                // Send success response
                ws_broadcast_json("settings_update_status", "{\"success\":true}");
            } else {
                // Send error response
                char error_msg[100];
                snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"%s\"}", esp_err_to_name(err));
                ws_broadcast_json("settings_update_status", error_msg);
            }
            
            free(new_settings);
        }
    }
    
    cJSON_Delete(root);
}
