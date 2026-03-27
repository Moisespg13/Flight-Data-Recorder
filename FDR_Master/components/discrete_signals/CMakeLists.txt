idf_component_register(SRCS "discrete_signals.c"
                    INCLUDE_DIRS "include"
                    REQUIRES master_config
                    PRIV_REQUIRES driver log)
