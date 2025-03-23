#include "ws_server.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include <cJSON.h>
#include "../utils/storage.h"
#include "../ble/ble_hid_device.h"
#include "esp_gap_ble_api.h"

static const char *WS_TAG = "WS";
static httpd_handle_t server = NULL;

#define MAX_CLIENTS CONFIG_LWIP_MAX_LISTENING_TCP
#define WS_MAX_MESSAGE_LEN 512  
#define WS_SMALL_MESSAGE_LEN 128

typedef struct {
    int *fds;
    int *failed;
    int failed_count;
    size_t max_clients;
    httpd_ws_frame_t frame;
} ws_client_ctx_t;

static ws_client_ctx_t *client_ctx = NULL;

static bool is_failed_client(const int fd) {
    if (!client_ctx) return false;
    for (int i = 0; i < client_ctx->failed_count; i++) {
        if (client_ctx->failed[i] == fd) {
            return true;
        }
    }
    return false;
}

static void add_failed_client(const int fd) {
    if (!client_ctx) return;
    if (is_failed_client(fd)) return;
    
    if (client_ctx->failed_count < client_ctx->max_clients) {
        client_ctx->failed[client_ctx->failed_count++] = fd;
    }
}

esp_err_t ws_send_frame_to_all_clients(const char *data, const size_t len) {
    if (!server || !client_ctx) {
        return ESP_FAIL;
    }

    size_t fds = client_ctx->max_clients;
    const esp_err_t ret = httpd_get_client_list(server, &fds, client_ctx->fds);
    if (ret != ESP_OK) {
        return ret;
    }

    client_ctx->frame.payload = (uint8_t*)data;
    client_ctx->frame.len = len;

    for (int i = 0; i < fds; i++) {
        if (is_failed_client(client_ctx->fds[i])) {
            continue;
        }
        
        const int client_info = httpd_ws_get_fd_info(server, client_ctx->fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            const esp_err_t err = httpd_ws_send_frame_async(server, client_ctx->fds[i], &client_ctx->frame);
            if (err != ESP_OK) {
                ESP_LOGW(WS_TAG, "Failed to send WS frame to client %d", client_ctx->fds[i]);
                httpd_sess_trigger_close(server, client_ctx->fds[i]);
                add_failed_client(client_ctx->fds[i]);
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


extern void process_wifi_ws_message(const char* message);
static void process_settings_ws_message(const char* message);

static void remove_failed_client(const int fd) {
    for (int i = 0; i < client_ctx->failed_count; i++) {
        if (client_ctx->failed[i] == fd) {
            if (i < client_ctx->failed_count - 1) {
                client_ctx->failed[i] = client_ctx->failed[client_ctx->failed_count - 1];
            }
            client_ctx->failed_count--;
            break;
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(WS_TAG, "Handshake done, the new connection was opened");

        const int sockfd = httpd_req_to_sockfd(req);
        if (sockfd != -1) {
            ESP_LOGI(WS_TAG, "New WebSocket client connected: %d", sockfd);
            remove_failed_client(sockfd);
        }
        
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    static uint8_t frame_buffer[WS_MAX_MESSAGE_LEN];
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
        if (ret == ESP_ERR_INVALID_STATE || ret == HTTPD_SOCK_ERR_FAIL) {
            const int sockfd = httpd_req_to_sockfd(req);
            ESP_LOGI(WS_TAG, "Client %d appears to be disconnected, closing session", sockfd);
            
            if (sockfd != -1) {
                add_failed_client(sockfd);
            }
            
            return ESP_FAIL;
        }
        return ret;
    }

    if (ws_pkt.len && ws_pkt.len < WS_MAX_MESSAGE_LEN) {
        ws_pkt.payload = frame_buffer;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
            if (ret == ESP_ERR_INVALID_STATE || ret == HTTPD_SOCK_ERR_FAIL) {
                const int sockfd = httpd_req_to_sockfd(req);
                ESP_LOGI(WS_TAG, "Client %d appears to be disconnected during frame receive, closing session", sockfd);
                
                if (sockfd != -1) {
                    add_failed_client(sockfd);
                }
                
                return ESP_FAIL; 
            }
            return ret;
        }
        frame_buffer[ws_pkt.len] = '\0';
        ESP_LOGI(WS_TAG, "Got packet with message: %s", frame_buffer);
        
        process_settings_ws_message((const char *)frame_buffer);
        process_wifi_ws_message((const char *)frame_buffer);

        const esp_err_t send_ret = httpd_ws_send_frame(req, &ws_pkt);
        if (send_ret != ESP_OK) {
            ESP_LOGW(WS_TAG, "Failed to echo WS frame, error: %d. Closing connection.", send_ret);

            const int sockfd = httpd_req_to_sockfd(req);
            if (sockfd != -1) {
                add_failed_client(sockfd);
            }
            
            return ESP_FAIL;
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

void init_websocket(const httpd_handle_t server_handle) {
    server = server_handle;
    
    client_ctx = calloc(1, sizeof(ws_client_ctx_t));
    if (!client_ctx) {
        ESP_LOGE(WS_TAG, "Failed to allocate client context");
        return;
    }
    
    client_ctx->max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    client_ctx->fds = calloc(client_ctx->max_clients, sizeof(int));
    client_ctx->failed = calloc(client_ctx->max_clients, sizeof(int));
    
    if (!client_ctx->fds || !client_ctx->failed) {
        ESP_LOGE(WS_TAG, "Failed to allocate client arrays");
        free(client_ctx->fds);
        free(client_ctx->failed);
        free(client_ctx);
        client_ctx = NULL;
        return;
    }
    
    client_ctx->failed_count = 0;
    client_ctx->frame.final = true;
    client_ctx->frame.fragmented = false;
    client_ctx->frame.type = HTTPD_WS_TYPE_TEXT;
    
    ESP_LOGI(WS_TAG, "Registering WebSocket handler");
    httpd_register_uri_handler(server, &ws);
}

void ws_queue_message(const char *data) {
    if (!data || !server) return;
    
    size_t len = strlen(data);
    if (len >= WS_MAX_MESSAGE_LEN) {
        ESP_LOGW(WS_TAG, "Message too long, truncating");
        len = WS_MAX_MESSAGE_LEN - 1;
    }

    const esp_err_t err = ws_send_frame_to_all_clients(data, len);
    if (err != ESP_OK) {
        ESP_LOGW(WS_TAG, "Failed to send message to clients: %s", esp_err_to_name(err));
    }
}

void ws_log(const char* text) {
    if (!text) return;
    char *buffer = malloc(WS_SMALL_MESSAGE_LEN);
    if (!buffer) return;
    
    const int len = snprintf(buffer, WS_SMALL_MESSAGE_LEN, "{\"type\":\"log\",\"content\":\"%s\"}", text);
    if (len > 0 && len < sizeof(buffer)) {
        ws_send_frame_to_all_clients(buffer, len);
        free(buffer);
    }
}

esp_err_t init_device_settings(void) {
    return init_global_settings();
}

static void remove_all_bonded_devices(void)
{
    int dev_num = esp_ble_get_bond_device_num();

    esp_ble_bond_dev_t *dev_list = malloc(sizeof(esp_ble_bond_dev_t) * dev_num);
    esp_ble_get_bond_device_list(&dev_num, dev_list);
    for (int i = 0; i < dev_num; i++) {
        esp_ble_remove_bond_device(dev_list[i].bd_addr);
    }

    free(dev_list);
}

static bool has_low_power_mode_changed(const char* new_settings_json, bool* new_value) {
    if (!new_settings_json || !new_value) return false;
    
    cJSON *root = cJSON_Parse(new_settings_json);
    if (!root) {
        ESP_LOGE(WS_TAG, "Error parsing new settings JSON");
        return false;
    }
    
    cJSON *power_obj = cJSON_GetObjectItem(root, "power");
    if (!power_obj) {
        ESP_LOGE(WS_TAG, "power object not found in new settings");
        cJSON_Delete(root);
        return false;
    }
    
    cJSON *low_power_mode = cJSON_GetObjectItem(power_obj, "lowPowerMode");
    if (!low_power_mode || !cJSON_IsBool(low_power_mode)) {
        ESP_LOGE(WS_TAG, "lowPowerMode not found or not a boolean in new settings");
        cJSON_Delete(root);
        return false;
    }
    
    *new_value = cJSON_IsTrue(low_power_mode);
    
    bool current_value = false;
    const esp_err_t err = storage_get_bool_setting("power.lowPowerMode", &current_value);
    
    cJSON_Delete(root);
    
    if (err != ESP_OK) {
        return true;
    }
    
    return *new_value != current_value;
}

static void process_settings_ws_message(const char* message) {
    if (!message) return;
    
    cJSON *root = cJSON_Parse(message);
    if (!root) {
        ESP_LOGE(WS_TAG, "Error parsing JSON message");
        return;
    }
    
    cJSON *type_obj = cJSON_GetObjectItem(root, "type");
    if (!type_obj || !cJSON_IsString(type_obj)) {
        ESP_LOGE(WS_TAG, "Missing or invalid 'type' field in message");
        cJSON_Delete(root);
        return;
    }
    
    const char *type = type_obj->valuestring;
    
    if (strcmp(type, "wifi_check_saved") == 0) {
        cJSON_Delete(root);
        return;
    }
    
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
            
            char *new_settings = cJSON_PrintUnformatted(content_obj);
            if (!new_settings) {
                ESP_LOGE(WS_TAG, "Error converting settings to string");
                cJSON_Delete(root);
                return;
            }
            
            bool new_low_power_mode = false;
            bool low_power_mode_changed = has_low_power_mode_changed(new_settings, &new_low_power_mode);

            const esp_err_t err = storage_update_settings(new_settings);
            
            if (err == ESP_OK) {
                ws_broadcast_json("settings_update_status", "{\"success\":true}");
                
                if (low_power_mode_changed) {
                    ESP_LOGI(WS_TAG, "lowPowerMode changed to %s, disconnecting BLE and unbonding devices", 
                             new_low_power_mode ? "true" : "false");
                    
                    ble_hid_device_deinit();
                    
                    remove_all_bonded_devices();
                    
                    ble_hid_device_init();
                    ble_hid_device_start_advertising();
                }
            } else {
                char error_msg[100];
                snprintf(error_msg, sizeof(error_msg), "{\"success\":false,\"error\":\"%s\"}", esp_err_to_name(err));
                ws_broadcast_json("settings_update_status", error_msg);
            }
            
            free(new_settings);
            storage_set_boot_with_wifi();
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
    }
    
    cJSON_Delete(root);
}
