idf_component_register(SRCS "spi_link.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES fdr_common esp_driver_spi driver log freertos esp_rom)
