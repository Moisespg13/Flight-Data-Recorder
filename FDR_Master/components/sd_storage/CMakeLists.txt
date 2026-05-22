idf_component_register(SRCS "sd_storage.c"
                    INCLUDE_DIRS "include" "../../../FDR_Common"
                    REQUIRES master_config
                    PRIV_REQUIRES fatfs sdmmc vfs log)
