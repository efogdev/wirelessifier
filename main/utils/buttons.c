#include "buttons.h"
#include "const.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "storage.h"

static button_click_callback_t user_click_callback = NULL;
static button_long_press_callback_t user_long_press_callback = NULL;
static int s_long_press_threshold = 1500;

static void buttons_task(void* arg);

void buttons_init() {
    storage_get_int_setting("buttons.longPressMs", &s_long_press_threshold);
    xTaskCreatePinnedToCore(buttons_task, "buttons_task", VERBOSE ? 2600 : 2350, NULL, 8, NULL, 1);
}

void buttons_subscribe_click(const button_click_callback_t callback) {
    user_click_callback = callback;
}

void buttons_subscribe_long_press(const button_long_press_callback_t callback) {
    user_long_press_callback = callback;
}

void buttons_deinit() {
    user_click_callback = NULL;
    user_long_press_callback = NULL;
}

static void buttons_task(void* arg) {
    uint32_t press_start_time[4] = {0};
    uint32_t last_state_time[4] = {0};
    bool is_pressed[4] = {false};
    bool long_press_detected[4] = {false};
    bool last_state[4] = {true};  

    while (1) {
        const uint32_t current_time = pdTICKS_TO_MS(xTaskGetTickCount());
        for (uint8_t i = 0; i < 4; i++) {
            const bool current_state = gpio_get_level(GPIO_BUTTON_SW1 + i);
            
            if (current_time - last_state_time[i] >= 20) {
                if (current_state != last_state[i]) {
                    last_state_time[i] = current_time;
                    last_state[i] = current_state;

                    if (!current_state) { // Button pressed (active low)
                        if (!is_pressed[i]) {
                            is_pressed[i] = true;
                            press_start_time[i] = current_time;
                            long_press_detected[i] = false;
                        }
                    } else { // Button released
                        if (is_pressed[i]) {
                            if (!long_press_detected[i] && user_click_callback &&
                                (current_time - press_start_time[i]) < s_long_press_threshold) {
                                user_click_callback(i);
                            }
                            is_pressed[i] = false;
                            long_press_detected[i] = false;
                            press_start_time[i] = 0;
                        }
                    }
                }
            }

            if (is_pressed[i] && !long_press_detected[i] && 
                (current_time - press_start_time[i]) >= s_long_press_threshold) {
                long_press_detected[i] = true;
                if (user_long_press_callback) {
                    user_long_press_callback(i);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
