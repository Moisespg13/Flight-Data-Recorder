idf_component_register(SRCS "spi_manager.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_spi
                    PRIV_REQUIRES driver log esp_rom)
