idf_component_register(SRCS "spi_link.c" "spi_manager.c"
                    INCLUDE_DIRS "include" "../../../FDR_Common"
                    REQUIRES master_config
                    PRIV_REQUIRES esp_driver_spi driver log freertos)
