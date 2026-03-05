idf_component_register(SRCS "slave_logic.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES fast_signals fdr_common spi_link log freertos driver)
