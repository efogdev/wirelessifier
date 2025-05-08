#include "ota_server.h"
#include <stdio.h>
#include <inttypes.h>
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_app_format.h"
#include "nvs.h"
#include "wifi_manager.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define BUFFSIZE 512

static const char *OTA_TAG = "OTA";
static char ota_write_data[BUFFSIZE + 1] = {0};
static char progress_str[32];

extern void ws_broadcast_small_json(const char *type, const char *content);

static void report_ota_progress(const int progress) {
    snprintf(progress_str, sizeof(progress_str), "{\"progress\":%d}", progress);
    ws_broadcast_small_json("ota_progress", progress_str);
}

static esp_ota_handle_t update_handle = 0;
static const esp_partition_t *update_partition = NULL;
static bool image_header_was_checked = false;

static esp_err_t handle_ota_upload(httpd_req_t *req) {
    esp_err_t err;
    update_handle = 0;
    image_header_was_checked = false;

    const esp_partition_t *running = esp_ota_get_running_partition();
    update_partition = esp_ota_get_next_update_partition(running);
    if (update_partition == NULL) {
        return ESP_FAIL;
    }

    const int total_size = req->content_len;
    int prev_progress = 0;
    int remaining = total_size;
    int received = 0;

    while (remaining > 0) {
        const int recv_len = MIN(remaining, BUFFSIZE);
        if ((received = httpd_req_recv(req, ota_write_data, recv_len)) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            if (update_handle) {
                esp_ota_abort(update_handle);
            }
            return ESP_FAIL;
        }

        if (image_header_was_checked == false) {
            esp_app_desc_t new_app_info;
            if (received > sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                memcpy(&new_app_info, &ota_write_data[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));

                err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                if (err != ESP_OK) {
                    ESP_LOGW(OTA_TAG, "OTA failed: %s", esp_err_to_name(err));
                    return err;
                }

                image_header_was_checked = true;
            }
        }

        if (update_handle) {
            err = esp_ota_write(update_handle, ota_write_data, received);
            if (err != ESP_OK) {
                ESP_LOGW(OTA_TAG, "Write failed: %s", esp_err_to_name(err));
                esp_ota_abort(update_handle);
                return err;
            }
        }

        remaining -= received;

        const int progress = ((total_size - remaining) * 100) / total_size;
        if (prev_progress != progress) {
            report_ota_progress(progress);
        }
        prev_progress = progress;

        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (!update_handle) {
        return ESP_FAIL;
    }

    err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        ESP_LOGW(OTA_TAG, "OTA finish failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        return err;
    }
    
    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OTA update successful");

    nvs_handle_t nvs_handle;
    if (nvs_open("ota", NVS_READWRITE, &nvs_handle) == ESP_OK) {
        nvs_set_u8(nvs_handle, "fw_updated", 1);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    nvs_handle_t nvs_handle2;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle2);
    if (err == ESP_OK) {
        nvs_set_u8(nvs_handle2, NVS_KEY_BOOT_WITH_WIFI, 1);
        nvs_commit(nvs_handle2);
        nvs_close(nvs_handle2);
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_upload_handler(httpd_req_t *req) {
    const esp_err_t err = handle_ota_upload(req);
    if (err != ESP_OK) {
        char response[BUFFSIZE];
        sprintf(response, "OTA update failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, response);
    }
    return err;
}

static const httpd_uri_t ota_upload = {
    .uri = "/upload",
    .method = HTTP_POST,
    .handler = ota_upload_handler,
    .user_ctx = NULL
};

void init_ota_server(const httpd_handle_t server) {
    httpd_register_uri_handler(server, &ota_upload);
}
