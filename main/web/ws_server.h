#pragma once

#include <esp_http_server.h>

// Settings namespace and keys for NVS
#define SETTINGS_NVS_NAMESPACE "device_settings"
#define SETTINGS_NVS_KEY "settings"

/**
 * @brief Initialize the WebSocket server
 * 
 * @param server_handle Handle to the HTTP server instance
 */
void init_websocket(httpd_handle_t server_handle);

/**
 * @brief Initialize device settings from NVS or defaults
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t init_device_settings(void);

/**
 * @brief Send a frame to all connected WebSocket clients
 * 
 * @param data The data to send
 * @param len Length of the data
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ws_send_frame_to_all_clients(const char *data, size_t len);

/**
 * @brief Broadcast a JSON message to all connected clients
 * 
 * @param type Message type identifier
 * @param content JSON content string
 */
void ws_broadcast_json(const char *type, const char *content);

/**
 * @brief Queue a message for broadcast to all clients
 * 
 * @param data Message to broadcast
 */
void ws_queue_message(const char *data);

/**
 * @brief Log a message through WebSocket
 * 
 * @param text Message to log
 */
void ws_log(const char* text);
