#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_clk_tree.h"
#include "task_monitor.h"
#include "hid_bridge.h"
#include "temp_sensor.h"

static const char *TAG = "mon";

#define STATS_TICKS         pdMS_TO_TICKS(1000)
#define MAX_TASKS           24
#define STATS_TASK_PRIO     3

static TaskStatus_t start_array[MAX_TASKS];
static TaskStatus_t end_array[MAX_TASKS];
static TaskHandle_t monitor_task_handle = NULL;

#define HEADER_FORMAT " Task (core %d)   |     Took |     | Free "
#define HEADER_SEPARATOR "-----------------|----------|-----|------"

static void print_core_tasks(const UBaseType_t start_array_size, const UBaseType_t end_array_size,
                           const uint32_t total_elapsed_time, const uint8_t core_id)
{
    printf(HEADER_FORMAT "\n", core_id);
    printf(HEADER_SEPARATOR "\n");

    uint32_t idle_time = 0;
    uint32_t core_total_time = 0;

    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xCoreID != core_id) continue;
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        if (k >= 0) {
            const uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            const uint32_t percentage_time = (task_elapsed_time * 100UL) / total_elapsed_time;
            const uint32_t task_elapsed_ms = task_elapsed_time / 1000;
            const UBaseType_t stack_high_water = end_array[k].usStackHighWaterMark;
            const UBaseType_t bytes_free = stack_high_water * sizeof(StackType_t);
            
            if (strncmp(start_array[i].pcTaskName, "IDLE", 4) == 0) {
                idle_time = task_elapsed_time;
            }
            core_total_time += task_elapsed_time;
            
            printf(" %-16s| %5"PRIu32" ms | %2"PRIu32"%% | %d \n",
                    start_array[i].pcTaskName, task_elapsed_ms, percentage_time, bytes_free);
        }
    }

    if (core_total_time > 0) {
        const uint32_t core_load = ((core_total_time - idle_time) * 100UL) / core_total_time;
        printf(HEADER_SEPARATOR "\n");
        printf(" Core load: %"PRIu32"%%\n", core_load);
    }
}

static esp_err_t print_real_time_stats(const TickType_t xTicksToWait)
{
    uint32_t start_run_time, end_run_time;

    const UBaseType_t start_array_size = uxTaskGetSystemState(start_array, MAX_TASKS, &start_run_time);
    if (start_array_size == 0 || start_array_size > MAX_TASKS) {
        return ESP_ERR_INVALID_SIZE;
    }

    vTaskDelay(xTicksToWait);

    const UBaseType_t end_array_size = uxTaskGetSystemState(end_array, MAX_TASKS, &end_run_time);
    if (end_array_size == 0 || end_array_size > MAX_TASKS) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    vTaskDelay(1);
    printf("\n");
    printf("     === Task monitor reporting ===\n");
    printf("\n");
    print_core_tasks(start_array_size, end_array_size, total_elapsed_time, 0);
    vTaskDelay(1);
    printf("\n");
    print_core_tasks(start_array_size, end_array_size, total_elapsed_time, 1);
    printf("\n");
    
    return ESP_OK;
}

static void monitor_task(void *pvParameter)
{
    while (1) {
        float tsens_value = 0;
        if (temp_sensor_get_temperature(&tsens_value) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read temperature");
        }

        if (print_real_time_stats(STATS_TICKS) != ESP_OK) {
            ESP_LOGE(TAG, "Error getting real time stats");
        }

        const size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
        printf("[I] Heap: %d kb, SoC temp: %.1f°C\n", free_heap / 1024, tsens_value);
        printf("\n");

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

esp_err_t task_monitor_init(void)
{
    temp_sensor_init();
    return ESP_OK;
}

esp_err_t task_monitor_start(void)
{
    if (monitor_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const BaseType_t ret = xTaskCreatePinnedToCore(monitor_task, "monitor", 2200, NULL, STATS_TASK_PRIO, &monitor_task_handle, 1);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
