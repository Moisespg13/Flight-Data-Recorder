idf_component_register(SRCS "sd_storage.c"
                    INCLUDE_DIRS "include" "../../../FDR_Common"
                    REQUIRES master_config
                    PRIV_REQUIRES fatfs sdmmc vfs esp_timer esp_rom esp_driver_spi esp_driver_sdspi log)
