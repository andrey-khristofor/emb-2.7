idf_component_register(SRCS "panic_sensor.c"
                    INCLUDE_DIRS "include"
                    REQUIRES
                        esp_driver_gpio
                    PRIV_REQUIRES
                        freertos
                        log
                    )
