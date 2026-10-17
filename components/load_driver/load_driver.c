#include <stdio.h>
#include "include/load_driver.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"
#include "esp_log.h"
#include "esp_err.h"

#if LOAD_BUZZER_MODE == LOAD_BUZZER_MODE_TRANSISTOR
#include "driver/ledc.h"
#endif

static const char *TAG = "load_driver";

#define LOAD_RELAY_ACTIVE_LOW 1

static inline int relay_level(bool on)
{
#if LOAD_RELAY_ACTIVE_LOW
    return on ? 0 : 1;
#else
    return on ? 1 : 0;
#endif
}

static int s_relay_last_level = -1;

#if LOAD_BUZZER_MODE == LOAD_BUZZER_MODE_TRANSISTOR
#define BUZZER_LEDC_TIMER LEDC_TIMER_0
#define BUZZER_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_CHANNEL LEDC_CHANNEL_0
#define BUZZER_LEDC_FREQ_HZ 2000
#define BUZZER_LEDC_RES LEDC_TIMER_8_BIT

static uint8_t s_buzzer_volume_percent = 60;
#endif

void load_driver_init()
{

    gpio_config_t cfg = {
#if LOAD_BUZZER_MODE == LOAD_BUZZER_MODE_RELAY
        .pin_bit_mask = (1ULL << LOAD_RELAY_PIN),
#else
        .pin_bit_mask = (1ULL << LOAD_RELAY_PIN),
#endif
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_set_level(LOAD_RELAY_PIN, relay_level(false));
    ESP_ERROR_CHECK(gpio_config(&cfg));
    load_relay_set(false);

#if LOAD_BUZZER_MODE == LOAD_BUZZER_MODE_TRANSISTOR
    ledc_timer_config_t timer_cfg = {
        .speed_mode = BUZZER_LEDC_MODE,
        .duty_resolution = BUZZER_LEDC_RES,
        .timer_num = BUZZER_LEDC_TIMER,
        .freq_hz = BUZZER_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_cfg);

    ledc_channel_config_t channel_cfg = {
        .gpio_num = LOAD_BUZZER_PIN,
        .speed_mode = BUZZER_LEDC_MODE,
        .channel = BUZZER_LEDC_CHANNEL,
        .timer_sel = BUZZER_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channel_cfg);
#endif
}

void load_relay_set(bool on)
{
    int level = relay_level(on);
    gpio_set_level(LOAD_RELAY_PIN, level);
    if (level != s_relay_last_level)
    {
        s_relay_last_level = level;
        ESP_LOGI(TAG, "RELAY GPIO%d -> %d (logical %s)", LOAD_RELAY_PIN, level, on ? "ON" : "OFF");
    }
}

#if LOAD_BUZZER_MODE == LOAD_BUZZER_MODE_RELAY

void load_buzzer_set(bool on)
{
    load_relay_set(on);
}

#else

void load_buzzer_set_volume(uint8_t percent)
{
    if (percent > 100)
        percent = 100;
    s_buzzer_volume_percent = percent;
}

void load_buzzer_set(bool on)
{
    uint32_t max_duty = (1u << BUZZER_LEDC_RES) - 1u;
    uint32_t duty = on ? (max_duty * s_buzzer_volume_percent) / 100u : 0u;
    static uint32_t s_last_duty = UINT32_MAX;
    if (duty == s_last_duty)
        return;
    s_last_duty = duty;
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, duty);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
    ESP_LOGI(TAG, "BUZZER GPIO%d PWM duty=%lu/%lu (volume=%u%%, %s)",
             LOAD_BUZZER_PIN, (unsigned long)duty, (unsigned long)max_duty,
             s_buzzer_volume_percent, on ? "ON" : "OFF");
}

#endif

void load_buzzer_chirp(uint32_t ms)
{
    load_buzzer_set(true);
    vTaskDelay(pdMS_TO_TICKS(ms));
    load_buzzer_set(false);
}
