idf_component_register(SRCS "button_fsm.c"
            INCLUDE_DIRS "include"
            REQUIRES
            	esp_driver_gpio
            	esp_timer
            )
