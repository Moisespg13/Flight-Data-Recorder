idf_component_register(SRCS "master_logic.c"
                    INCLUDE_DIRS "include" "../../../FDR_Common"
                    REQUIRES master_config
                    PRIV_REQUIRES spi_link discrete_signals rtc_ds3232 sd_storage gnss imu_xsens cmd_port supervisor sample_format log freertos driver esp_timer nvs_flash heap esp_psram)
