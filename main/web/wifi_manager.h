#pragma once

#include <esp_err.h>
#include <stdbool.h>

// Maximum number of connection retry attempts
#define MAX_RETRY 3

// NVS keys
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"
#define NVS_KEY_BOOT_WITH_WIFI "boot_wifi"

/**
 * @brief Connect to WiFi using credentials stored in NVS
 * 
 * @return esp_err_t ESP_OK on success, ESP_FAIL if connection fails
 */
esp_err_t connect_wifi_with_stored_credentials(void);

/**
 * @brief Save WiFi credentials to NVS
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return esp_err_t ESP_OK on success
 */
esp_err_t save_wifi_credentials(const char* ssid, const char* password);

/**
 * @brief Clear WiFi credentials from NVS
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clear_wifi_credentials(void);

/**
 * @brief Check if WiFi credentials are stored in NVS
 * 
 * @return true if credentials are stored
 * @return false if no credentials are stored
 */
bool has_wifi_credentials(void);

/**
 * @brief Scan for available WiFi networks and broadcast results via WebSocket
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t scan_wifi_networks(void);

/**
 * @brief Connect to a specific WiFi network
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password (can be NULL for open networks)
 * @return esp_err_t ESP_OK on success, ESP_FAIL if connection fails
 */
esp_err_t connect_to_wifi(const char* ssid, const char* password);

/**
 * @brief Process WebSocket messages for WiFi management
 * 
 * @param message WebSocket message to process
 */
void process_wifi_ws_message(const char* message);

/**
 * @brief Update WiFi connection status
 * 
 * @param connected Connection status
 * @param ip IP address (can be NULL if disconnected)
 */
void update_wifi_connection_status(bool connected, const char* ip);

/**
 * @brief Check if device is connected to WiFi
 * 
 * @return true if connected
 * @return false if not connected
 */
bool is_wifi_connected(void);

/**
 * @brief Start the WebSocket ping task
 * 
 * This task sends periodic ping messages to all connected WebSocket clients
 * to keep the connection alive and prevent timeouts.
 */
void start_ws_ping_task(void);
