#include "button_fsm.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"

static inline bool raw_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == 1;
}

void button_fsm_init(button_fsm_t *btn, gpio_num_t pin)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));

    btn->pin = pin;
    btn->state = BTN_STATE_IDLE;
    btn->state_enter_ms = 0;
    btn->held_since_ms = 0;
    btn->long_fired = false;
}

button_event_t button_fsm_tick(button_fsm_t *btn, uint32_t now_ms)
{
    bool pressed = raw_pressed(btn->pin);
    button_event_t event = BTN_EVENT_NONE;

    switch (btn->state)
    {
    case BTN_STATE_IDLE:
        if (pressed)
        {
            btn->state = BTN_STATE_PRESSED;
            btn->state_enter_ms = now_ms;
        }
        break;

    case BTN_STATE_PRESSED:
        if (!pressed)
        {
            btn->state = BTN_STATE_IDLE;
        }
        else if (now_ms - btn->state_enter_ms >= BTN_DEBONCE_MS)
        {
            btn->state = BTN_STATE_HELD;
            btn->state_enter_ms = now_ms;
            btn->held_since_ms = now_ms;
            btn->long_fired = false;
            event = BTN_EVENT_PRESS;
        }
        break;

    case BTN_STATE_HELD:
        if (!pressed)
        {
            btn->state = BTN_STATE_RELEASE;
            btn->state_enter_ms = now_ms;
        }
        else if (!btn->long_fired && now_ms - btn->held_since_ms >= BTN_LONG_PRESS_MS)
        {
            event = BTN_EVENT_LONG_PRESS;
            btn->long_fired = true;
        }
        break;

    case BTN_STATE_RELEASE:
        if (pressed)
        {
            btn->state = BTN_STATE_HELD;
        }
        else if (now_ms - btn->state_enter_ms >= BTN_DEBONCE_MS)
        {
            if (!btn->long_fired)
                event = BTN_EVENT_SHORT_PRESS;
            btn->state = BTN_STATE_IDLE;
        }
        break;
    }
    return event;
}

static void fsm_timer_cb(void *arg)
{
    button_fsm_tick_cb_t tick_cb = (button_fsm_tick_cb_t)arg;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    tick_cb(now_ms);
}

void button_fsm_start_timer(button_fsm_tick_cb_t tick_cb)
{
    const esp_timer_create_args_t args = {
        .callback = &fsm_timer_cb,
        .arg = (void *)tick_cb,
        .name = "fsm_tick",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, BTN_FSM_TICK_MS * 1000ULL));
}
