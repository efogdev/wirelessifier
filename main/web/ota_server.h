#pragma once

#include <esp_http_server.h>

/**
 * @brief Initialize the OTA update server
 * 
 * This function registers the necessary HTTP handlers for handling firmware
 * updates over HTTP. It sets up endpoints for receiving firmware binary files
 * and manages the OTA update process.
 * 
 * @param server Handle to the HTTP server instance
 */
void init_ota_server(httpd_handle_t server);
