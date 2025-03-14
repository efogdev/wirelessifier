#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Start the DNS server for captive portal functionality
 * 
 * @param dns_task_handle Pointer to store the created task handle
 */
void start_dns_server(TaskHandle_t *dns_task_handle);
