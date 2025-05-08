#include "rotary_enc.h"
#include "const.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "storage.h"

#define PCNT_HIGH_LIMIT 8
#define PCNT_LOW_LIMIT  (-8)

static QueueHandle_t enc_queue = NULL;
static QueueHandle_t button_state_queue = NULL;
static rotary_callback_t user_callback = NULL;
static rotary_click_callback_t user_click_callback = NULL;
static rotary_long_press_callback_t user_long_press_callback = NULL;
static pcnt_unit_handle_t pcnt_unit = NULL;
static int s_long_press_threshold = 1500;

static void rotary_enc_task(void* arg);

static bool pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup = pdFALSE;
    const QueueHandle_t queue = (QueueHandle_t)user_ctx;
    const int8_t direction = edata->watch_point_value > 0 ? (edata->watch_point_value/(PCNT_HIGH_LIMIT/2)) : (edata->watch_point_value/(PCNT_LOW_LIMIT/2)*-1);
    xQueueSendFromISR(queue, &direction, &high_task_wakeup);
    return (high_task_wakeup == pdTRUE);
}

static void IRAM_ATTR click_isr_handler(void* arg) {
    const uint8_t state = gpio_get_level(GPIO_ROT_E);
    xQueueSendFromISR(button_state_queue, &state, NULL);
}

void rotary_enc_init() {
    enc_queue = xQueueCreate(2, sizeof(int8_t));
    button_state_queue = xQueueCreate(2, sizeof(uint8_t));

    const pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_config, &pcnt_unit));

    const pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 5000,
    };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config));

    const pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = GPIO_ROT_A,
        .level_gpio_num = GPIO_ROT_B,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_a_config, &pcnt_chan_a));

    const pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = GPIO_ROT_B,
        .level_gpio_num = GPIO_ROT_A,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(pcnt_unit, &chan_b_config, &pcnt_chan_b));

    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE));
    ESP_ERROR_CHECK(pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

    const int watch_points[] = {PCNT_LOW_LIMIT, PCNT_LOW_LIMIT/2, PCNT_HIGH_LIMIT/2, PCNT_HIGH_LIMIT};
    for (size_t i = 0; i < sizeof(watch_points) / sizeof(watch_points[0]); i++) {
        ESP_ERROR_CHECK(pcnt_unit_add_watch_point(pcnt_unit, watch_points[i]));
    }

    const pcnt_event_callbacks_t cbs = {
        .on_reach = pcnt_on_reach,
    };
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, enc_queue));

    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    storage_get_int_setting("buttons.longPressMs", &s_long_press_threshold);
    if (s_long_press_threshold < 750) {
        s_long_press_threshold = 750;
    }

    gpio_isr_handler_add(GPIO_ROT_E, click_isr_handler, NULL);
    xTaskCreatePinnedToCore(rotary_enc_task, "rotary_task", VERBOSE ? 2300 : 1850, NULL, 8, NULL, 1);
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
    if (pcnt_unit) {
        pcnt_unit_stop(pcnt_unit);
        pcnt_unit_disable(pcnt_unit);
        pcnt_del_unit(pcnt_unit);
    }
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
    bool is_pressed = false;
    bool long_press_detected = false;
    
    while (1) {
        const uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (xQueueReceive(enc_queue, &direction, 0)) {
            if (user_callback) {
                user_callback(direction);
            }
            ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
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
            (current_time - press_start_time) >= s_long_press_threshold) {
            long_press_detected = true;
            if (user_long_press_callback) {
                user_long_press_callback();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
