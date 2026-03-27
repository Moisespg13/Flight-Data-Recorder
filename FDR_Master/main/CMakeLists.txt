idf_component_register(SRCS "FDR_Master.c"
                    INCLUDE_DIRS "include" "../../FDR_Common"
                    PRIV_REQUIRES master_logic spi_link discrete_signals nvs_flash master_config)
