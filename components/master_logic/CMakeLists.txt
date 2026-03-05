idf_component_register(SRCS "master_logic.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES fdr_common spi_link log freertos driver)
