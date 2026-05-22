idf_component_register(SRCS "imu_xsens.c"
                    INCLUDE_DIRS "include" "../../../FDR_Common"
                    REQUIRES master_config
                    PRIV_REQUIRES driver esp_driver_uart esp_timer log)
