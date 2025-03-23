#pragma once

#include <esp_err.h>
#include <stdbool.h>

// Maximum number of connection retry attempts
#define MAX_RETRY 5

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
 * @brief Scan for available WiFi networks asynchronously
 * Results will be processed in the WIFI_EVENT_SCAN_DONE event handler
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t scan_wifi_networks(void);

/**
 * @brief Process WiFi scan results and broadcast them via WebSocket
 * This function is called from the WIFI_EVENT_SCAN_DONE event handler
 */
void process_wifi_scan_results(void);

/**
 * @brief Connect to a specific WiFi network
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password (can be NULL for open networks)
 * @return esp_err_t ESP_OK on success, ESP_FAIL if connection fails
 */
esp_err_t connect_to_wifi(const char* ssid, const char* password);

/**
 * @brief Disable WiFi and web stack
 * 
 * This function disables WiFi and web stack, and clears the boot with WiFi flag.
 */
void disable_wifi_and_web_stack(void);

/**
 * @brief Reboot the device
 * 
 * @param keep_wifi Whether to keep WiFi and web stack on after reboot
 */
void reboot_device(bool keep_wifi);

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
 * @brief Check if WiFi and web stack are enabled
 *
 * @return true if yes
 * @return false if not
 */
bool is_wifi_enabled(void);

/**
 * @brief Start the WebSocket ping task
 * 
 * This task sends periodic ping messages to all connected WebSocket clients
 * to keep the connection alive and prevent timeouts.
 */
void start_ws_ping_task(void);
