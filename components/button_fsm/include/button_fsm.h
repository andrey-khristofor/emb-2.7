#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "driver/gpio.h"
#include "stdint.h"
#include "stdbool.h"

    typedef enum
    {
        BTN_STATE_IDLE = 0,
        BTN_STATE_PRESSED,
        BTN_STATE_HELD,
        BTN_STATE_RELEASE,
    } button_state_t;

    typedef enum
    {
        BTN_EVENT_NONE = 0,
        BTN_EVENT_PRESS,
        BTN_EVENT_SHORT_PRESS,
        BTN_EVENT_LONG_PRESS,
    } button_event_t;

    typedef struct
    {
        gpio_num_t pin;
        button_state_t state;
        uint32_t state_enter_ms;
        uint32_t held_since_ms;
        bool long_fired;
    } button_fsm_t;

#define BTN_DEBONCE_MS 50u
#define BTN_LONG_PRESS_MS 2000u
#define BTN_FSM_TICK_MS 10u

    typedef void (*button_fsm_tick_cb_t)(uint32_t now_ms);

    void button_fsm_init(button_fsm_t *btn, gpio_num_t pin);
    button_event_t button_fsm_tick(button_fsm_t *btn, uint32_t now_ms);
    void button_fsm_start_timer(button_fsm_tick_cb_t tick_cb);

#ifdef __cplusplus
}
#endif
