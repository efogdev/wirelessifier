#include "wifi_manager.h"
#include <string.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <storage.h>
#include <cJSON.h>

#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "ws_server.h"
#include "http_server.h"
#include "temp_sensor.h"
#include "rgb_leds.h"
#include "vmon.h"

static const char *WIFI_TAG = "WIFI_MGR";

#define WS_PING_TASK_STACK_SIZE 2250
#define WS_PING_TASK_PRIORITY 4
#define WS_PING_INTERVAL_MS 250

#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"

int s_retry_num = 0;
static bool connecting = false;
static bool s_web_stack_disabled = false;
static TaskHandle_t ping_task_handle = NULL;

static bool is_connected = false;
static char connected_ip[16] = {0};

esp_err_t connect_wifi_with_stored_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }

    size_t ssid_len = 32; // Max SSID length
    char ssid[33] = {0};  // +1 for null terminator
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "No stored SSID found: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    size_t pass_len = 64; // Max password length
    char password[65] = {0}; // +1 for null terminator
    err = nvs_get_str(nvs_handle, NVS_KEY_PASS, password, &pass_len);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "No stored password found: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    nvs_close(nvs_handle);

    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    
    ESP_LOGI(WIFI_TAG, "Connecting to %s...", ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    const esp_err_t connect_err = esp_wifi_connect();
    if (connect_err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Failed to connect to WiFi: %s", esp_err_to_name(connect_err));
        return connect_err;
    }
    
    const EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(WIFI_TAG, "Connected to %s", ssid);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(WIFI_TAG, "Failed to connect to %s", ssid);
        return ESP_FAIL;
    } else {
        ESP_LOGE(WIFI_TAG, "Unexpected event");
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t save_wifi_credentials(const char* ssid, const char* password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error saving SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error saving password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Error committing NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

esp_err_t clear_wifi_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(nvs_handle, NVS_KEY_SSID);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_erase_key(nvs_handle, NVS_KEY_PASS);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    return err;
}

bool has_wifi_credentials(void) {
    if (connecting) {
        return true;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    size_t ssid_len = 0;
    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, NULL, &ssid_len);
    nvs_close(nvs_handle);
    
    return (err == ESP_OK && ssid_len > 0);
}

void process_wifi_scan_results(void) {
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    
    ESP_LOGI(WIFI_TAG, "WiFi scan completed, found %d networks", ap_count);
    
    if (ap_count == 0) {
        ESP_LOGI(WIFI_TAG, "No networks found");
        ws_broadcast_small_json("wifi_scan_result", "[]");
        return;
    }
    
    wifi_ap_record_t *ap_records = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (ap_records == NULL) {
        ESP_LOGE(WIFI_TAG, "Failed to allocate memory for scan results");
        return;
    }
    
    char *json_buffer = malloc(ap_count * 128);
    if (json_buffer == NULL) {
        free(ap_records);
        ESP_LOGE(WIFI_TAG, "Failed to allocate memory for JSON buffer");
        return;
    }
    
    if (esp_wifi_scan_get_ap_records(&ap_count, ap_records) != ESP_OK) {
        free(ap_records);
        free(json_buffer);
        ESP_LOGE(WIFI_TAG, "Failed to get AP records");
        return;
    }
    
    int offset = 0;
    offset += sprintf(json_buffer + offset, "[");
    
    for (int i = 0; i < ap_count; i++) {
        char ssid_escaped[64] = {0};
        size_t ssid_len = strnlen((const char *)ap_records[i].ssid, sizeof(ap_records[i].ssid));
        
        for (int j = 0, k = 0; j < ssid_len && k < 63; j++) {
            if (ap_records[i].ssid[j] == '"' || ap_records[i].ssid[j] == '\\') {
                ssid_escaped[k++] = '\\';
            }
            ssid_escaped[k++] = ap_records[i].ssid[j];
        }
        
        offset += sprintf(json_buffer + offset, 
                         "%s{\"ssid\":\"%s\",\"rssi\":%d,\"authmode\":%d}",
                         i > 0 ? "," : "",
                         ssid_escaped,
                         ap_records[i].rssi,
                         ap_records[i].authmode);
    }

    ws_broadcast_json("wifi_scan_result", json_buffer);
    free(json_buffer);
    free(ap_records);
}

esp_err_t scan_wifi_networks(void) {
    ESP_LOGI(WIFI_TAG, "Starting WiFi scan...");
    esp_wifi_scan_stop();
    
    const wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 200,
        .scan_time.active.max = 600
    };
    
    const esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
    if (ret != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Failed to start WiFi scan: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t connect_to_wifi(const char* ssid, const char* password) {
    if (!ssid || strlen(ssid) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (connecting) {
        return ESP_OK;
    }

    connecting = true;
    s_retry_num = 0;
    if (is_connected) {
        esp_wifi_disconnect();
    }
    
    wifi_config_t wifi_config = {0};
    strlcpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    
    if (password) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        strlcpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    }
    
    ESP_LOGI(WIFI_TAG, "Connecting to %s...", ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    
    const esp_err_t connect_err = esp_wifi_connect();
    if (connect_err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "Failed to connect to WiFi: %s", esp_err_to_name(connect_err));
        return connect_err;
    }
    
    const EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, 40000 / portTICK_PERIOD_MS);
    
    connecting = false;
    static char status_json[128];

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(WIFI_TAG, "Connected to %s", ssid);
        save_wifi_credentials(ssid, password ? password : "");
        snprintf(status_json, sizeof(status_json), 
                "{\"connected\":true,\"ip\":\"%s\",\"attempt\":%d}", 
                connected_ip, s_retry_num);
        ws_broadcast_small_json("wifi_connect_status", status_json);
        storage_set_boot_with_wifi();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();}

    if (bits & WIFI_FAIL_BIT) {
        snprintf(status_json, sizeof(status_json),
                "{\"connected\":false,\"attempt\":%d}",
                s_retry_num);
        ws_broadcast_small_json("wifi_connect_status", status_json);
        ESP_LOGI(WIFI_TAG, "Failed to connect to %s", ssid);
        return ESP_FAIL;
    }

    snprintf(status_json, sizeof(status_json), "{\"connected\":false,\"attempt\":%d}", s_retry_num);
    ws_broadcast_small_json("wifi_connect_status", status_json);
    ESP_LOGE(WIFI_TAG, "Connection timeout");
    return ESP_ERR_TIMEOUT;
}

void disable_wifi_and_web_stack(void) {
    ESP_LOGI(WIFI_TAG, "Disabling WiFi and web stack...");
 
    s_web_stack_disabled = true;
    is_connected = false;
    s_retry_num = MAX_RETRY;

    led_update_wifi_status(false, false);
    led_update_status(0, 0);

    ws_broadcast_small_json("web_stack_disabled", "{}");
    vTaskDelay(pdMS_TO_TICKS(250));

    if (ping_task_handle != NULL && eTaskGetState(ping_task_handle) != eDeleted) {
        vTaskDelete(ping_task_handle);
        ping_task_handle = NULL;
    }

    stop_webserver();
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_deinit();

    nvs_handle_t nvs_handle;
    const esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle, NVS_KEY_BOOT_WITH_WIFI, 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(WIFI_TAG, "Cleared boot with WiFi flag");
    }

    led_update_wifi_status(false, false);
    led_update_status(0, 0);

    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }

    ESP_LOGI(WIFI_TAG, "WiFi and web stack disabled and cleaned up");
}

void reboot_device(bool keep_wifi) {
    ESP_LOGI(WIFI_TAG, "Rebooting device, keep_wifi=%d", keep_wifi);
    ws_broadcast_small_json("reboot", "{}");
    vTaskDelay(pdMS_TO_TICKS(250)); // Give time for the messages to be sent
    
    nvs_handle_t nvs_handle;
    const esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle, NVS_KEY_BOOT_WITH_WIFI, keep_wifi ? 1 : 0);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
        ESP_LOGI(WIFI_TAG, "%s boot with WiFi flag", keep_wifi ? "Set" : "Cleared");
    }
    
    vTaskDelay(pdMS_TO_TICKS(20));
    esp_restart();
}

void process_wifi_ws_message(const char* message) {
    cJSON *root = cJSON_Parse(message);
    if (!root) {
        ESP_LOGE(WIFI_TAG, "Failed to parse JSON message");
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!type || !type->valuestring) {
        cJSON_Delete(root);
        return;
    }

    static char status_json[32];
    if (strcmp(type->valuestring, "wifi_check_saved") == 0) {
        const bool has_creds = has_wifi_credentials();
        snprintf(status_json, sizeof(status_json), "%s", has_creds ? "true" : "false");
        ws_broadcast_small_json("wifi_saved_credentials", status_json);
    }
    else if (strcmp(type->valuestring, "wifi_scan") == 0) {
        scan_wifi_networks();
    }
    else if (strcmp(type->valuestring, "reboot") == 0) {
        const cJSON *keepWifi = cJSON_GetObjectItem(root, "keepWifi");
        const bool keep_wifi = keepWifi && cJSON_IsTrue(keepWifi);
        reboot_device(keep_wifi);
    }
    else if (strcmp(type->valuestring, "disable_web_stack") == 0) {
        // disable_wifi_and_web_stack();
        ws_broadcast_small_json("web_stack_disabled", "{}");
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart(); // because no memory freed when disabling web stack for some reason (ToDo)
    }
    else if (strcmp(type->valuestring, "wifi_connect") == 0) {
        cJSON *content = cJSON_GetObjectItem(root, "content");
        if (!content) {
            cJSON_Delete(root);
            return;
        }

        cJSON *ssid_json = cJSON_GetObjectItem(content, "ssid");
        const cJSON *password_json = cJSON_GetObjectItem(content, "password");

        if (!ssid_json || !ssid_json->valuestring) {
            cJSON_Delete(root);
            return;
        }

        const char *ssid = ssid_json->valuestring;
        const char *password = password_json ? password_json->valuestring : "";

        if (strlen(ssid) > 0) {
            connect_to_wifi(ssid, password);
        }
    }  else if (strcmp(type->valuestring, "ota_confirm") == 0) {
        esp_ota_mark_app_valid_cancel_rollback();

        nvs_handle_t nvs_handle;
        if (nvs_open("ota", NVS_READWRITE, &nvs_handle) == ESP_OK) {
            nvs_set_u8(nvs_handle, "fw_updated", 0);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
        }

        esp_restart();
    }

    cJSON_Delete(root);
}

void update_wifi_connection_status(bool connected, const char* ip) {
    if (s_web_stack_disabled) {
        led_update_wifi_status(false, false);
        led_update_status(0, 0);
        return;
    }

    is_connected = connected;
    if (ip) {
        strlcpy(connected_ip, ip, sizeof(connected_ip));
    }
    
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);
    bool is_apsta_mode = (mode == WIFI_MODE_APSTA);
    
    led_update_wifi_status(is_apsta_mode, connected);
}

bool is_wifi_connected(void) {
    return is_connected;
}

bool is_wifi_enabled(void) {
    return !s_web_stack_disabled;
}

static void ws_ping_task(void *pvParameters) {
    ESP_LOGI(WIFI_TAG, "WebSocket ping task started");
    
    while (1) {
        if (s_web_stack_disabled) {
            vTaskDelay(pdMS_TO_TICKS(WS_PING_INTERVAL_MS));
            continue;
        }

        const uint32_t free_heap = esp_get_free_heap_size();
        static float temp = 0;
        temp_sensor_get_temperature(&temp);
        const float bat = get_battery_level();
        
        static char ping_data[96];
        snprintf(ping_data, sizeof(ping_data), "{\"heap\":%lu,\"temp\":%.1f,\"bat\":%.2f}", free_heap, temp, bat);
        
        ws_broadcast_small_json("ping", ping_data);
        vTaskDelay(pdMS_TO_TICKS(WS_PING_INTERVAL_MS));
    }
    
    vTaskDelete(NULL);
}

void start_ws_ping_task(void) {
    if (ping_task_handle == NULL) {
        const BaseType_t result = xTaskCreatePinnedToCore(ws_ping_task,
            "ws_ping_task", WS_PING_TASK_STACK_SIZE, NULL, WS_PING_TASK_PRIORITY, &ping_task_handle, 0);
        
        if (result != pdPASS) {
            ESP_LOGE(WIFI_TAG, "Failed to create WebSocket ping task");
        } else {
            ESP_LOGI(WIFI_TAG, "WebSocket ping task created");
        }
    } else {
        ESP_LOGW(WIFI_TAG, "WebSocket ping task already running");
    }
}
