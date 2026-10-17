idf_component_register(SRCS "load_driver.c"
                    INCLUDE_DIRS "include"
                    REQUIRES
            	        esp_driver_gpio
            	        esp_driver_ledc
                    )
