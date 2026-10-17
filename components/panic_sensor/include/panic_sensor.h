#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "driver/gpio.h"

#define PANIC_PIN GPIO_NUM_7

    void panic_sensor_init(void (*on_panic_deferred)(void));

#ifdef __cplusplus
}
#endif
