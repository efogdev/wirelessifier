#include "ws_server.h"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *WS_TAG = "WS";
static httpd_handle_t server = NULL;
static QueueHandle_t ws_queue = NULL;

#define MAX_CLIENTS CONFIG_LWIP_MAX_LISTENING_TCP
#define WS_QUEUE_SIZE 10
#define WS_QUEUE_TIMEOUT pdMS_TO_TICKS(100)

typedef struct {
    char *data;
    size_t len;
} ws_message_t;

esp_err_t ws_send_frame_to_all_clients(const char *data, size_t len) {
    if (!server) {
        return ESP_FAIL;
    }

    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    esp_err_t ret = httpd_get_client_list(server, &fds, client_fds);
    if (ret != ESP_OK) {
        return ret;
    }

    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t*)data,
        .len = len
    };

    for (int i = 0; i < fds; i++) {
        int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server, client_fds[i], &ws_pkt);
        }
    }

    return ESP_OK;
}

void ws_broadcast_json(const char *type, const char *content) {
    if (!type || !content) return;
    
    char *buffer;
    asprintf(&buffer, "{\"type\":\"%s\",\"content\":%s}", type, content);
    if (buffer) {
        ws_send_frame_to_all_clients(buffer, strlen(buffer));
        free(buffer);
    }
}

static void ws_queue_task(void *pvParameters) {
    ws_message_t msg;
    
    while (1) {
        if (xQueueReceive(ws_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.data) {
                ws_send_frame_to_all_clients(msg.data, msg.len);
                free(msg.data);
            }
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
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            ESP_LOGE(WS_TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            ESP_LOGE(WS_TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        ESP_LOGI(WS_TAG, "Got packet with message: %s", ws_pkt.payload);
    }
    
    // Echo the message back
    ret = httpd_ws_send_frame(req, &ws_pkt);
    if (buf) {
        free(buf);
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
    xTaskCreatePinnedToCore(ws_queue_task, "ws_queue", 2048, NULL, 5, NULL, 1);
    httpd_register_uri_handler(server, &ws);
}

// Function to queue a message for broadcast
void ws_queue_message(const char *data) {
    if (!data || !ws_queue) return;
    
    ws_message_t msg;
    msg.data = strdup(data);
    msg.len = strlen(data);
    
    if (xQueueSend(ws_queue, &msg, WS_QUEUE_TIMEOUT) != pdTRUE) {
        free(msg.data);
        ESP_LOGE(WS_TAG, "Failed to queue message");
    }
}

// Helper function to log messages through websocket
void ws_log(const char* text) {
    if (!text) return;
    char *buffer;
    asprintf(&buffer, "{\"type\":\"log\",\"content\":\"%s\"}", text);
    if (buffer) {
        ws_send_frame_to_all_clients(buffer, strlen(buffer));
        free(buffer);
    }
}
