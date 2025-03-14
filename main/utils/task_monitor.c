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

#define STATS_TICKS         pdMS_TO_TICKS(1000)
#define ARRAY_SIZE_OFFSET   5   //Increase this if print_real_time_stats returns ESP_ERR_INVALID_SIZE
#define STATS_TASK_PRIO     3

static TaskHandle_t monitor_task_handle = NULL;
static temperature_sensor_handle_t temp_sensor = NULL;

static esp_err_t print_real_time_stats(TickType_t xTicksToWait)
{
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    esp_err_t ret;

    //Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = malloc(sizeof(TaskStatus_t) * start_array_size);
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    vTaskDelay(xTicksToWait);

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = malloc(sizeof(TaskStatus_t) * end_array_size);
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    ESP_LOGI(TAG, " Task         |         Took |     | Free, bytes ");
    ESP_LOGI(TAG, "--------------|--------------|-----|-------------");
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
            
            ESP_LOGI(TAG, "%-14s| %9"PRIu32" ms | %3"PRIu32"%% | %10d ",
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
    ret = ESP_OK;

exit:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}

static void monitor_task(void *pvParameter)
{
    while (1) {
        ESP_LOGI(TAG, "");

        // Get and print heap info
        size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        ESP_LOGI(TAG, "Free heap: %zu bytes", free_heap);
        
        // Get and print temperature
        float tsens_value;
        if (temperature_sensor_get_celsius(temp_sensor, &tsens_value) == ESP_OK) {
            ESP_LOGI(TAG, "SOC temperature: %.1f°C", tsens_value);
        } else {
            ESP_LOGE(TAG, "Failed to read temperature");
        }
        
        // Get and print CPU frequency
        uint32_t cpu_freq;
        esp_err_t freq_ret = esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT, &cpu_freq);
        if (freq_ret == ESP_OK) {
            ESP_LOGI(TAG, "CPU frequency: %"PRIu32" MHz", cpu_freq / 1000000); // Convert to MHz
        } else {
            ESP_LOGE(TAG, "Failed to get CPU frequency");
        }

        ESP_LOGI(TAG, "");
        if (print_real_time_stats(STATS_TICKS) != ESP_OK) {
            ESP_LOGE(TAG, "Error getting real time stats");
        }
        
        // Report 5 times less frequently if BLE is paused
        uint32_t delay_ms = 5000; // Default: print stats every 5 seconds
        if (hid_bridge_is_ble_paused()) {
            delay_ms = 25000; // 5 times less frequent (25 seconds)
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

    BaseType_t ret = xTaskCreatePinnedToCore(monitor_task, "monitor", 2600, NULL, STATS_TASK_PRIO, &monitor_task_handle, tskNO_AFFINITY);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
