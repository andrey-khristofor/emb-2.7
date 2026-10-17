#pragma once
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define LOAD_BUZZER_MODE_RELAY 1
#define LOAD_BUZZER_MODE_TRANSISTOR 2

#define LOAD_BUZZER_MODE LOAD_BUZZER_MODE_RELAY

#define LOAD_BUZZER_PIN GPIO_NUM_11
#define LOAD_RELAY_PIN GPIO_NUM_12

    void load_driver_init(void);
    void load_relay_set(bool on);
    void load_buzzer_set(bool on);
    void load_buzzer_chirp(uint32_t ms);

#if LOAD_BUZZER_MODE == LOAD_BUZZER_MODE_TRANSISTOR
    void load_buzzer_set_volume(uint8_t percent);
#endif

#ifdef __cplusplus
}
#endif