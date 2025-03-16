#include "http_server.h"
#include "dns_server.h"
#include "ws_server.h"
#include "ota_server.h"
#include "wifi_manager.h"
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
EventGroupHandle_t wifi_event_group;

// Default WiFi configuration
#define WIFI_SSID      "AnyBLE WEB"
#define WIFI_CHANNEL   1
#define MAX_CONN       4

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static esp_err_t root_get_handler(httpd_req_t *req)
{
    extern const uint8_t web_front_index_html_start[] asm("_binary_index_html_start");
    extern const uint8_t web_front_index_html_end[] asm("_binary_index_html_end");
    const size_t index_html_size = (web_front_index_html_end - web_front_index_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char*)web_front_index_html_start, index_html_size);
    
    return ESP_OK;
}

static esp_err_t settings_get_handler(httpd_req_t *req)
{
    extern const uint8_t web_front_settings_html_start[] asm("_binary_settings_html_start");
    extern const uint8_t web_front_settings_html_end[] asm("_binary_settings_html_end");
    const size_t settings_html_size = (web_front_settings_html_end - web_front_settings_html_start);
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char*)web_front_settings_html_start, settings_html_size);
    
    return ESP_OK;
}

static esp_err_t lib_get_handler(httpd_req_t *req)
{
    const char *req_uri = req->uri;
    
    if (strstr(req_uri, "react.production.min.js")) {
        extern const uint8_t web_front_lib_react_production_min_js_start[] asm("_binary_react_production_min_js_start");
        extern const uint8_t web_front_lib_react_production_min_js_end[] asm("_binary_react_production_min_js_end");
        const size_t js_size = (web_front_lib_react_production_min_js_end - web_front_lib_react_production_min_js_start) - 1;
        
        httpd_resp_set_type(req, "application/javascript");
        httpd_resp_send(req, (const char*)web_front_lib_react_production_min_js_start, js_size);
    } 
    else if (strstr(req_uri, "react-dom.production.min.js")) {
        extern const uint8_t web_front_lib_react_dom_production_min_js_start[] asm("_binary_react_dom_production_min_js_start");
        extern const uint8_t web_front_lib_react_dom_production_min_js_end[] asm("_binary_react_dom_production_min_js_end");
        const size_t js_size = (web_front_lib_react_dom_production_min_js_end - web_front_lib_react_dom_production_min_js_start) - 1;
        
        httpd_resp_set_type(req, "application/javascript");
        httpd_resp_send(req, (const char*)web_front_lib_react_dom_production_min_js_start, js_size);
    }
    else if (strstr(req_uri, "babel.min.js")) {
        extern const uint8_t web_front_lib_babel_min_js_start[] asm("_binary_babel_min_js_start");
        extern const uint8_t web_front_lib_babel_min_js_end[] asm("_binary_babel_min_js_end");
        const size_t js_size = (web_front_lib_babel_min_js_end - web_front_lib_babel_min_js_start) - 1;
        
        httpd_resp_set_type(req, "application/javascript");
        httpd_resp_send(req, (const char*)web_front_lib_babel_min_js_start, js_size);
    }
    else {
        return ESP_ERR_NOT_FOUND;
    }
    
    return ESP_OK;
}

// Redirect handler for captive portal
static esp_err_t redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t settings = {
    .uri = "/settings",
    .method = HTTP_GET,
    .handler = settings_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t lib = {
    .uri = "/lib/*",
    .method = HTTP_GET,
    .handler = lib_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t redirect = {
    .uri = "/*",
    .method = HTTP_GET,
    .handler = redirect_handler,
    .user_ctx = NULL
};

// External reference to retry counter
extern int s_retry_num;

// Forward declaration for the scan completion handler
extern void process_wifi_scan_results(void);

static void event_handler(void* arg, esp_event_base_t event_base,
    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (has_wifi_credentials()) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(HTTP_TAG, "WIFI_EVENT_STA_DISCONNECTED");
        
        // Increment retry counter
        s_retry_num++;
        
        if (s_retry_num < MAX_RETRY) {
            ESP_LOGI(HTTP_TAG, "Retry to connect to the AP, attempt %d/%d", s_retry_num, MAX_RETRY);
            esp_wifi_connect();
        } else {
            ESP_LOGI(HTTP_TAG, "Failed to connect after %d attempts", MAX_RETRY);
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
        }
        
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        update_wifi_connection_status(false, NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        ESP_LOGI(HTTP_TAG, "WIFI_EVENT_SCAN_DONE");
        process_wifi_scan_results();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(HTTP_TAG, "Got IP: %s", ip_str);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        update_wifi_connection_status(true, ip_str);
    }
}

void init_wifi_apsta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Always create both netifs regardless of credentials
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .max_connection = MAX_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };

    if (has_wifi_credentials()) {
        // Use STA mode when credentials exist
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_LOGI(HTTP_TAG, "WiFi initialized in STA mode.");
    } else {
        // Use APSTA mode when no credentials
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
        ESP_LOGI(HTTP_TAG, "WiFi initialized in APSTA mode.");
    }

    ESP_ERROR_CHECK(esp_wifi_start());
}

httpd_handle_t start_webserver(void)
{
    if (server != NULL) {
        ESP_LOGI(HTTP_TAG, "Server already running");
        return server;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 7;
    config.stack_size = 6200;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 3;
    config.send_wait_timeout = 3;

    ESP_LOGI(HTTP_TAG, "Starting server on port: '%d'", config.server_port);
    
    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &settings);
        httpd_register_uri_handler(server, &lib);

        // Initialize websocket server
        init_websocket(server);
        
        // Start WebSocket ping task
        start_ws_ping_task();
        
        // Initialize OTA server
        init_ota_server(server);
        
        // Start DNS server for captive portal if in AP mode
        if (!has_wifi_credentials() || esp_wifi_get_mode(NULL) == WIFI_MODE_APSTA) {
            start_dns_server(&dns_task_handle);
        }

        // Other
        httpd_register_uri_handler(server, &redirect);

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

static void web_services_task(void *pvParameters)
{
    ESP_LOGI(HTTP_TAG, "Initializing web services in task");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize WiFi AP+STA
    init_wifi_apsta();
    
    // Check if we have stored WiFi credentials
    if (has_wifi_credentials()) {
        // Try to connect with stored credentials
        ESP_LOGI(HTTP_TAG, "Found stored WiFi credentials, attempting to connect");
    
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdTRUE, 10000 / portTICK_PERIOD_MS);

        if (bits & WIFI_FAIL_BIT) {
            // Set one-time boot flag to start with WiFi and reboot
            nvs_handle_t nvs_handle;
            esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
            if (err == ESP_OK) {
                nvs_set_u8(nvs_handle, NVS_KEY_BOOT_WITH_WIFI, 1);
                nvs_commit(nvs_handle);
                nvs_close(nvs_handle);
                ESP_LOGI(HTTP_TAG, "Set boot with WiFi flag");
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }
        }

    }
    
    // Start the web server
    start_webserver();
    
    // Task should remain running to keep the web server alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void init_web_services(void)
{
    ESP_LOGI(HTTP_TAG, "Starting web services task");
    wifi_event_group = xEventGroupCreate();
    xTaskCreatePinnedToCore(web_services_task, "web_services", 5200, NULL, 5, NULL, 1);
}
