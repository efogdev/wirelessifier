#pragma once

#include <esp_http_server.h>

/**
 * @brief Initialize WiFi in Access Point mode
 * 
 * Sets up the ESP32 as a WiFi access point with the configured SSID and password.
 */
void init_wifi_ap(void);

/**
 * @brief Start the HTTP web server
 * 
 * Initializes and starts the HTTP server, setting up all URI handlers
 * and initializing the websocket and OTA update functionality.
 * 
 * @return httpd_handle_t Handle to the created server, or NULL if failed
 */
httpd_handle_t start_webserver(void);

/**
 * @brief Stop the web server and cleanup resources
 * 
 * Stops the HTTP server and any associated tasks (DNS server, etc.)
 */
void stop_webserver(void);

/**
 * @brief Initialize all web services
 * 
 * This is the main initialization function that should be called to set up
 * all web-related services. It initializes NVS, WiFi AP, and starts the web server.
 */
void init_web_services(void);
