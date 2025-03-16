#include "http_server.h"
#include "dns_server.h"
#include "ws_server.h"
#include "ota_server.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *HTTP_TAG = "HTTP";
static httpd_handle_t server = NULL;
static TaskHandle_t dns_task_handle = NULL;

// Default WiFi configuration
#define WIFI_SSID      "AnyBLE WEB"
#define WIFI_CHANNEL   1
#define MAX_CONN       4

static esp_err_t root_get_handler(httpd_req_t *req)
{
    extern const uint8_t web_front_index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t web_front_index_html_end[] asm("_binary_index_html_end");
    const size_t index_html_size = (web_front_index_html_end - web_front_index_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char*)web_front_index_html_start, index_html_size);
    
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL
};

// Redirect handler for captive portal
static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t redirect = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = redirect_handler,
    .user_ctx = NULL
};

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    // if (event_id == WIFI_EVENT_AP_STACONNECTED) {
    //     wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
    //     ESP_LOGI(HTTP_TAG, "Station "MACSTR" joined, AID=%d",
    //              MAC2STR(event->mac), event->aid);
    // } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
    //     wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
    //     ESP_LOGI(HTTP_TAG, "Station "MACSTR" left, AID=%d",
    //              MAC2STR(event->mac), event->aid);
    // }
}

void init_wifi_ap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                      ESP_EVENT_ANY_ID,
                                                      &wifi_event_handler,
                                                      NULL,
                                                      NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .max_connection = MAX_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(HTTP_TAG, "WiFi AP initialized. SSID:%s channel:%d", WIFI_SSID, WIFI_CHANNEL);
}

httpd_handle_t start_webserver(void)
{
    if (server != NULL) {
        ESP_LOGI(HTTP_TAG, "Server already running");
        return server;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 3; // We only have root, redirect, and websocket
    config.stack_size = 1536;    // Reduced stack size as handlers are simple
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;  // Reduce timeout for faster resource cleanup
    config.send_wait_timeout = 3;

    ESP_LOGI(HTTP_TAG, "Starting server on port: '%d'", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &redirect);

        // Initialize websocket server
        init_websocket(server);
        
        // Initialize OTA server
        init_ota_server(server);
        
        // Start DNS server for captive portal
        start_dns_server(&dns_task_handle);
        
        return server;
    }

    ESP_LOGI(HTTP_TAG, "Error starting server!");
    return NULL;
}

void stop_webserver(void)
{
    if (server) {
        httpd_stop(server);
        server = NULL;
    }
    
    if (dns_task_handle) {
        vTaskDelete(dns_task_handle);
        dns_task_handle = NULL;
    }
}

void init_web_services(void)
{
    ESP_LOGI(HTTP_TAG, "Initializing web services");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize WiFi AP
    init_wifi_ap();
    
    // Start the web server
    start_webserver();
}
