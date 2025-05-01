#include "rotary_enc.h"
#include "const.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static QueueHandle_t enc_queue = NULL;
static rotary_callback_t user_callback = NULL;
static rotary_click_callback_t user_click_callback = NULL;
static rotary_long_press_callback_t user_long_press_callback = NULL;
static QueueHandle_t button_state_queue = NULL;

static void rotary_enc_task(void* arg);

static void IRAM_ATTR enc_isr_handler(void* arg) {
    static uint8_t prev_state = 0;
    const uint8_t curr_state = (gpio_get_level(GPIO_ROT_A) << 1) | gpio_get_level(GPIO_ROT_B);
    
    static const int8_t transitions[] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    const int8_t direction = transitions[(prev_state << 2) | curr_state];
    prev_state = curr_state;
    if (direction != 0) {
        xQueueSendFromISR(enc_queue, &direction, NULL);
    }
}

static void IRAM_ATTR click_isr_handler(void* arg) {
    const uint8_t state = gpio_get_level(GPIO_ROT_E); // 1 for pressed, 0 for released
    xQueueSendFromISR(button_state_queue, &state, NULL);
}

void rotary_enc_init() {
    enc_queue = xQueueCreate(4, sizeof(int8_t));
    button_state_queue = xQueueCreate(4, sizeof(uint8_t));
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_ROT_A, enc_isr_handler, NULL);
    gpio_isr_handler_add(GPIO_ROT_B, enc_isr_handler, NULL);
    gpio_isr_handler_add(GPIO_ROT_E, click_isr_handler, NULL);
    xTaskCreate(rotary_enc_task, "rotary_task", 2000, NULL, 14, NULL);
}

void rotary_enc_subscribe(const rotary_callback_t callback) {
    user_callback = callback;
}

void rotary_enc_subscribe_click(const rotary_click_callback_t callback) {
    user_click_callback = callback;
}

void rotary_enc_subscribe_long_press(const rotary_long_press_callback_t callback) {
    user_long_press_callback = callback;
}

void rotary_enc_deinit() {
    gpio_isr_handler_remove(GPIO_ROT_A);
    gpio_isr_handler_remove(GPIO_ROT_B);
    gpio_isr_handler_remove(GPIO_ROT_E);
    vQueueDelete(enc_queue);
    vQueueDelete(button_state_queue);
    user_callback = NULL;
    user_click_callback = NULL;
    user_long_press_callback = NULL;
}

static void rotary_enc_task(void* arg) {
    int8_t direction;
    uint8_t button_state;
    uint32_t last_click_time = 0;
    uint32_t press_start_time = 0;
    uint32_t last_enc_callback_time = 0;
    int8_t accumulated_value = 0;
    bool is_pressed = false;
    bool long_press_detected = false;
    
    while (1) {
        const uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (xQueueReceive(enc_queue, &direction, 0)) {
            accumulated_value += direction;
        }

        if (accumulated_value != 0 && 
            (current_time - last_enc_callback_time) >= 50 && 
            user_callback) {
            user_callback(accumulated_value > 0 ? 1 : -1);
            accumulated_value = 0;
            last_enc_callback_time = current_time;
        }

        if (xQueueReceive(button_state_queue, &button_state, 0)) {
            if (button_state) { // Button pressed
                if (!is_pressed) {
                    is_pressed = true;
                    press_start_time = current_time;
                    long_press_detected = false;
                }
            } else { // Button released
                if (is_pressed) {
                    is_pressed = false;
                    if (!long_press_detected && user_click_callback && 
                        (current_time - last_click_time) > 50) { // Debounce
                        user_click_callback();
                        last_click_time = current_time;
                    }
                }
            }
        }

        // Check for long press only while button is held
        if (is_pressed && !long_press_detected && 
            (current_time - press_start_time) >= 3000) {
            long_press_detected = true;
            if (user_long_press_callback) {
                user_long_press_callback();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
