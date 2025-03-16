#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/temperature_sensor.h"
#include "esp_clk_tree.h"
#include "task_monitor.h"
#include "../hid_bridge.h"

static const char *TAG = "TaskMonitor";

// USB HID report counter variables
static uint32_t g_usb_report_counter = 0;
static uint32_t g_usb_last_report_counter = 0;

#define STATS_TICKS         pdMS_TO_TICKS(1000)
#define MAX_TASKS           32  // Maximum number of tasks to monitor
#define STATS_TASK_PRIO     3

// Static allocations for task arrays
static TaskStatus_t start_array[MAX_TASKS];
static TaskStatus_t end_array[MAX_TASKS];
static TaskHandle_t monitor_task_handle = NULL;
static temperature_sensor_handle_t temp_sensor = NULL;

// Static strings for logging
static const char *const HEADER_FORMAT = " Task           |         Took |     | Free, bytes ";
static const char *const HEADER_SEPARATOR = "----------------|--------------|-----|-------------";

void task_monitor_increment_usb_report_counter(void)
{
    g_usb_report_counter++;
}

static esp_err_t print_real_time_stats(TickType_t xTicksToWait)
{
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;

    // Get current task states
    start_array_size = uxTaskGetSystemState(start_array, MAX_TASKS, &start_run_time);
    if (start_array_size == 0 || start_array_size > MAX_TASKS) {
        return ESP_ERR_INVALID_SIZE;
    }

    vTaskDelay(xTicksToWait);

    // Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, MAX_TASKS, &end_run_time);
    if (end_array_size == 0 || end_array_size > MAX_TASKS) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "%s", HEADER_FORMAT);
    ESP_LOGI(TAG, "%s", HEADER_SEPARATOR);
    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
            // Convert microseconds to milliseconds
            uint32_t task_elapsed_ms = task_elapsed_time / 1000;
            
            // Calculate free stack space and percentage
            UBaseType_t stack_high_water = end_array[k].usStackHighWaterMark;
            UBaseType_t bytes_free = stack_high_water * sizeof(StackType_t);
            
            ESP_LOGI(TAG, "%-16s| %9"PRIu32" ms | %2"PRIu32"%% | %d ",
                    start_array[i].pcTaskName, task_elapsed_ms, percentage_time, bytes_free);
        }
    }

    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            ESP_LOGI(TAG, "%-14s| Deleted", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            ESP_LOGI(TAG, "%-14s| Created", end_array[i].pcTaskName);
        }
    }
    return ESP_OK;
}

static void monitor_task(void *pvParameter)
{
    while (1) {
        ESP_LOGI(TAG, "");

        // Get and print heap info
        size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        ESP_LOGI(TAG, "Free heap: %d kb", free_heap / 1024);
        
        // Get and print temperature
        float tsens_value;
        if (temperature_sensor_get_celsius(temp_sensor, &tsens_value) == ESP_OK) {
            ESP_LOGI(TAG, "SOC temperature: %.1f°C", tsens_value);
        } else {
            ESP_LOGE(TAG, "Failed to read temperature");
        }

        ESP_LOGI(TAG, "");
        if (print_real_time_stats(STATS_TICKS) != ESP_OK) {
            ESP_LOGE(TAG, "Error getting real time stats");
        }
        
        // Print USB HID reports per second
        uint32_t reports = g_usb_report_counter - g_usb_last_report_counter;
        g_usb_last_report_counter = g_usb_report_counter;

        uint32_t delay_ms = 10000; 
        if (hid_bridge_is_ble_paused()) {
            delay_ms = 120000;
        } else if (reports > 0) {
            ESP_LOGI(TAG, "USB HID: %lu rps", reports / 10);
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t task_monitor_init(void)
{
    // Initialize temperature sensor
    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    esp_err_t ret = temperature_sensor_install(&temp_sensor_config, &temp_sensor);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = temperature_sensor_enable(temp_sensor);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t task_monitor_start(void)
{
    if (monitor_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t ret = xTaskCreatePinnedToCore(monitor_task, "monitor", 2048, NULL, STATS_TASK_PRIO, &monitor_task_handle, tskNO_AFFINITY);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
