idf_component_register(SRCS "rtc_ds3232.c"
                    INCLUDE_DIRS "include"
                    REQUIRES master_config
                    PRIV_REQUIRES driver log)
