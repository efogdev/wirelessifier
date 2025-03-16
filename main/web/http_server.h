#pragma once

#include <esp_http_server.h>
#include "freertos/event_groups.h"

// Expose event group for WiFi events
extern EventGroupHandle_t wifi_event_group;

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/**
 * @brief Initialize WiFi in Access Point mode
 * 
 * Sets up the ESP32 as a WiFi access point with the configured SSID and password.
 */
void init_wifi_apsta(void);

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
 * This function starts a task on core 1 with stack size 4096 that initializes
 * all web-related services including NVS, WiFi AP, and the web server.
 * The task remains running to keep the web server alive.
 */
void init_web_services(void);
