#include "buttons.h"
#include "const.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

static QueueHandle_t button_state_queue = NULL;
static button_click_callback_t user_click_callback = NULL;
static button_long_press_callback_t user_long_press_callback = NULL;

typedef struct {
    uint8_t button_index;
    uint8_t state;
} button_event_t;

static void buttons_task(void* arg);

static void IRAM_ATTR button_isr_handler(void* arg) {
    const uint8_t button_index = (uint32_t) arg;
    const button_event_t event = {
        .button_index = button_index,
        .state = gpio_get_level(GPIO_BUTTON_SW1 + button_index)
    };
    xQueueSendFromISR(button_state_queue, &event, NULL);
}

void buttons_init() {
    button_state_queue = xQueueCreate(8, sizeof(button_event_t));
    for (uint8_t i = 0; i < 4; i++) {
        gpio_isr_handler_add(GPIO_BUTTON_SW1 + i, button_isr_handler, (void*)(uint32_t)i);
    }
    
    xTaskCreate(buttons_task, "buttons_task", 2400, NULL, 6, NULL);
}

void buttons_subscribe_click(const button_click_callback_t callback) {
    user_click_callback = callback;
}

void buttons_subscribe_long_press(const button_long_press_callback_t callback) {
    user_long_press_callback = callback;
}

void buttons_deinit() {
    for (uint8_t i = 0; i < 4; i++) {
        gpio_isr_handler_remove(GPIO_BUTTON_SW1 + i);
    }
    vQueueDelete(button_state_queue);
    user_click_callback = NULL;
    user_long_press_callback = NULL;
}

static void buttons_task(void* arg) {
    button_event_t event;
    uint32_t last_click_time[4] = {0};
    uint32_t press_start_time[4] = {0};
    bool is_pressed[4] = {false};
    bool long_press_detected[4] = {false};
    
    while (1) {
        const uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (xQueueReceive(button_state_queue, &event, 0)) {
            const uint8_t idx = event.button_index;
            
            if (event.state) { // Button pressed
                if (!is_pressed[idx]) {
                    is_pressed[idx] = true;
                    press_start_time[idx] = current_time;
                    long_press_detected[idx] = false;
                }
            } else { // Button released
                if (is_pressed[idx]) {
                    is_pressed[idx] = false;
                    if (!long_press_detected[idx] && user_click_callback && 
                        (current_time - last_click_time[idx]) > 50) { // Debounce
                        user_click_callback(idx);
                        last_click_time[idx] = current_time;
                    }
                }
            }
        }

        // Check for long press on all buttons
        for (uint8_t i = 0; i < 4; i++) {
            if (is_pressed[i] && !long_press_detected[i] && 
                (current_time - press_start_time[i]) >= 3000) {
                long_press_detected[i] = true;
                if (user_long_press_callback) {
                    user_long_press_callback(i);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
