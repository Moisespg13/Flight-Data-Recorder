idf_component_register(SRCS "master_logic.c"
                    INCLUDE_DIRS "include" "../../../FDR_Common"
                    REQUIRES master_config
                    PRIV_REQUIRES spi_link discrete_signals log freertos driver esp_timer)
