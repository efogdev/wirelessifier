#include "ota_server.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_app_format.h"


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define BUFFSIZE 256
#define OTA_RECV_TIMEOUT 5000

static const char *OTA_TAG = "OTA";
static char ota_write_data[BUFFSIZE + 1] = {0};

// Forward declarations for external functions
extern void ws_log(const char* text);
extern void ws_broadcast_json(const char *type, const char *content);

static void report_ota_progress(int progress) {
    char progress_str[32];
    snprintf(progress_str, sizeof(progress_str), "{\"progress\":%d}", progress);
    ws_broadcast_json("ota_progress", progress_str);
}

static esp_err_t validate_image_header(esp_app_desc_t *new_app_info) {
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(OTA_TAG, "Running firmware version: %s", running_app_info.version);
    }

    const esp_partition_t *last_invalid_app = esp_ota_get_last_invalid_partition();
    esp_app_desc_t invalid_app_info;
    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
        ESP_LOGI(OTA_TAG, "Last invalid firmware version: %s", invalid_app_info.version);
    }

    return ESP_OK;
}

// Static variables for OTA state
static esp_ota_handle_t update_handle = 0;
static const esp_partition_t *update_partition = NULL;
static bool image_header_was_checked = false;
static int last_progress = 0;

static esp_err_t handle_ota_upload(httpd_req_t *req) {
    esp_err_t err;
    update_handle = 0;
    image_header_was_checked = false;
    last_progress = 0;

    ESP_LOGI(OTA_TAG, "Starting OTA update...");
    ws_log("Starting OTA update...");

    const esp_partition_t *running = esp_ota_get_running_partition();
    update_partition = esp_ota_get_next_update_partition(running);
    if (update_partition == NULL) {
        ws_log("Error: No OTA partition available");
        return ESP_FAIL;
    }

    ESP_LOGI(OTA_TAG, "Writing to partition subtype %d at offset 0x%" PRIx32,
             update_partition->subtype, update_partition->address);

    int total_size = req->content_len;
    int remaining = total_size;
    int received = 0;

    while (remaining > 0) {
        int recv_len = MIN(remaining, BUFFSIZE);
        if ((received = httpd_req_recv(req, ota_write_data, recv_len)) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ws_log("Error: Failed to receive file");
            return ESP_FAIL;
        }

        if (image_header_was_checked == false) {
            esp_app_desc_t new_app_info;
            if (received > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
                ESP_LOGI(OTA_TAG, "New firmware version: %s", new_app_info.version);
                ws_log("Validating firmware image...");
                
                err = validate_image_header(&new_app_info);
                if (err != ESP_OK) {
                    ws_log("Error: Invalid firmware image");
                    return err;
                }

                image_header_was_checked = true;

                err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                if (err != ESP_OK) {
                    ws_log("Error: Failed to begin OTA update");
                    return err;
                }
            }
        }

        err = esp_ota_write(update_handle, ota_write_data, received);
        if (err != ESP_OK) {
            ws_log("Error: Failed to write OTA data");
            esp_ota_abort(update_handle);
            return err;
        }

        remaining -= received;
        
        // Report progress every 10%
        int progress = ((total_size - remaining) * 100) / total_size;
        if (progress / 10 != last_progress / 10) {
            last_progress = progress;
            report_ota_progress(progress);
        }
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ws_log("Error: Failed to complete OTA update");
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ws_log("Image validation failed, image is corrupted");
        }
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ws_log("Error: Failed to set boot partition");
        return err;
    }

    ws_log("OTA update successful! Rebooting...");
    
    // Send success response before reboot
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OTA update successful");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

static esp_err_t ota_upload_handler(httpd_req_t *req) {
    esp_err_t err = handle_ota_upload(req);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA update failed");
    }
    return err;
}

static const httpd_uri_t ota_upload = {
    .uri = "/upload",
    .method = HTTP_POST,
    .handler = ota_upload_handler,
    .user_ctx = NULL
};

void init_ota_server(httpd_handle_t server) {
    ESP_LOGI(OTA_TAG, "Registering OTA upload handler");
    httpd_register_uri_handler(server, &ota_upload);
}
