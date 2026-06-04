idf_component_register(SRCS "cmd_port.c"
                    INCLUDE_DIRS "include"
                    REQUIRES master_config
                    PRIV_REQUIRES driver esp_driver_uart nvs_flash log sd_storage)
