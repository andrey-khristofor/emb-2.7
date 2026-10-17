idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "."
                        REQUIRES
    	                    esp_driver_gpio
    	                    esp_timer
    	                    esp_system
    	                    log
    	                    freertos    
    	                    button_fsm
    	                    panic_sensor
    	                    load_driver 
    	                    pot_sensor
    	                    nvs_flash
                        )
