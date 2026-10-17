idf_component_register(SRCS "pot_sensor.c"
                    INCLUDE_DIRS "include"
                    REQUIRES 
           		        esp_adc
            	        esp_timer 
                    )
