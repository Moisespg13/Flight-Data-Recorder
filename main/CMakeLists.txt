idf_component_register(SRCS "FDR.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES fdr_common fast_signals nvs_flash slave_logic spi_link master_logic)