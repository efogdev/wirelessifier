#include "ws_server.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *WS_TAG = "WS";
static httpd_handle_t server = NULL;
static QueueHandle_t ws_queue = NULL;

#define MAX_CLIENTS CONFIG_LWIP_MAX_LISTENING_TCP
#define WS_QUEUE_SIZE 5  // Reduced from 10 as it's sufficient for most use cases
#define WS_QUEUE_TIMEOUT pdMS_TO_TICKS(50)  // Reduced timeout
#define WS_MAX_MESSAGE_LEN 512  // Maximum message length to prevent excessive allocations

typedef struct {
    char data[WS_MAX_MESSAGE_LEN];
    size_t len;
} ws_message_t;  // Using fixed buffer to avoid heap fragmentation

// Static buffers for client handling
static int client_fds[CONFIG_LWIP_MAX_LISTENING_TCP];
static httpd_ws_frame_t broadcast_frame = {
    .final = true,
    .fragmented = false,
    .type = HTTPD_WS_TYPE_TEXT
};

esp_err_t ws_send_frame_to_all_clients(const char *data, size_t len) {
    if (!server) {
        return ESP_FAIL;
    }

    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;

    esp_err_t ret = httpd_get_client_list(server, &fds, client_fds);
    if (ret != ESP_OK) {
        return ret;
    }

    broadcast_frame.payload = (uint8_t*)data;
    broadcast_frame.len = len;

    for (int i = 0; i < fds; i++) {
        int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server, client_fds[i], &broadcast_frame);
        }
    }

    return ESP_OK;
}

void ws_broadcast_json(const char *type, const char *content) {
    if (!type || !content) return;
    
    char buffer[WS_MAX_MESSAGE_LEN];
    int len = snprintf(buffer, sizeof(buffer), "{\"type\":\"%s\",\"content\":%s}", type, content);
    if (len > 0 && len < sizeof(buffer)) {
        ws_send_frame_to_all_clients(buffer, len);
    }
}

static void ws_queue_task(void *pvParameters) {
    ws_message_t msg;
    
    while (1) {
        if (xQueueReceive(ws_queue, &msg, portMAX_DELAY) == pdTRUE) {
            ws_send_frame_to_all_clients(msg.data, msg.len);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

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
        
        // Echo the message back
        ret = httpd_ws_send_frame(req, &ws_pkt);
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
    
    ws_queue = xQueueCreate(WS_QUEUE_SIZE, sizeof(ws_message_t));
    if (ws_queue == NULL) {
        ESP_LOGE(WS_TAG, "Failed to create websocket queue");
        return;
    }

    ESP_LOGI(WS_TAG, "Registering WebSocket handler");
    xTaskCreatePinnedToCore(ws_queue_task, "ws_queue", 1500, NULL, 5, NULL, 1);  
    httpd_register_uri_handler(server, &ws);
}

void ws_queue_message(const char *data) {
    if (!data || !ws_queue) return;
    
    size_t len = strlen(data);
    if (len >= WS_MAX_MESSAGE_LEN) {
        ESP_LOGW(WS_TAG, "Message too long, truncating");
        len = WS_MAX_MESSAGE_LEN - 1;
    }
    
    ws_message_t msg;
    memcpy(msg.data, data, len);
    msg.data[len] = '\0';
    msg.len = len;
    
    if (xQueueSend(ws_queue, &msg, WS_QUEUE_TIMEOUT) != pdTRUE) {
        ESP_LOGE(WS_TAG, "Failed to queue message");
    }
}

void ws_log(const char* text) {
    if (!text) return;
    char buffer[WS_MAX_MESSAGE_LEN];
    int len = snprintf(buffer, sizeof(buffer), "{\"type\":\"log\",\"content\":\"%s\"}", text);
    if (len > 0 && len < sizeof(buffer)) {
        ws_send_frame_to_all_clients(buffer, len);
    }
}
