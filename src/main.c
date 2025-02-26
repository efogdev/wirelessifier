#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "neopixel.h"

#define GPIO_WS2812B_PIN GPIO_NUM_38
#define NUM_LEDS 17
#define ANIMATION_DELAY_MS 50

static tNeopixelContext *neopixel_ctx = NULL;

void running_light_animation(void) {
    static int position = 0;
    tNeopixel pixels[NUM_LEDS];
    
    for (int i = 0; i < NUM_LEDS; i++) {
        pixels[i].index = i;
        pixels[i].rgb = NP_RGB(0, 0, 0);
    }
    
    for (int i = 0; i < 3; i++) {
        int pos = (position + i) % NUM_LEDS;
        uint8_t intensity = 255 >> i; 
        pixels[pos].rgb = NP_RGB(0, 0, intensity / 8);
    }
    
    neopixel_SetPixel(neopixel_ctx, pixels, NUM_LEDS);
    position = (position + 1) % NUM_LEDS;
}

void led_control_task(void *pvParameters) {
    neopixel_ctx = neopixel_Init(NUM_LEDS, GPIO_WS2812B_PIN);
    if (neopixel_ctx == NULL) {
        printf("Failed to initialize NeoPixel\n");
        vTaskDelete(NULL);
        return;
    }
    
    while (1) {
        running_light_animation();
        vTaskDelay(pdMS_TO_TICKS(ANIMATION_DELAY_MS));
    }
}

void init_gpio(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (
            (1ULL<<GPIO_NUM_36) |
            (1ULL<<GPIO_NUM_35) |
            (1ULL<<GPIO_NUM_34) |
            (1ULL<<GPIO_NUM_33)
        ),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&io_conf);

    // CHARGE_EN: LOW = EN
    gpio_set_level(GPIO_NUM_36, 0);

    // USB_MUX_OE: LOW = EN
    gpio_set_level(GPIO_NUM_34, 0);

    // USB_MUX_SEL: 1 = PC, 0 = MC
    gpio_set_level(GPIO_NUM_33, 1);

    // PWR_LED: red, via 5.1kOhm
    // PWM to optimize battery life
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 32768,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = 35, // GPIO 35
        .duty           = 8, // barely but still visible
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}


void app_main(void)
{
    init_gpio();
    xTaskCreatePinnedToCore(led_control_task, "LED_Control", 4096, NULL, 5, NULL, 1);
    
    printf("Free heap: %d bytes\n", (int)esp_get_free_heap_size());
}
