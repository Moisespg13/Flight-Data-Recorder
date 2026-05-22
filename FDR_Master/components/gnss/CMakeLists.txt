idf_component_register(SRCS "gnss.c"
                    INCLUDE_DIRS "include"
                    REQUIRES master_config
                    PRIV_REQUIRES driver esp_driver_uart esp_driver_gpio esp_timer log)
