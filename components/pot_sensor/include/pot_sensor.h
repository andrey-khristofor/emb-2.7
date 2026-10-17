#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    void pot_sensor_init(void);
    uint16_t pot_sensor_get_raw(void);

#ifdef __cplusplus
}
#endif