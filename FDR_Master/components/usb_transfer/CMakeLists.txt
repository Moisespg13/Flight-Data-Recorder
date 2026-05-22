idf_component_register(SRCS "usb_transfer.c"
                    INCLUDE_DIRS "include"
                    REQUIRES sd_storage
                    PRIV_REQUIRES usb_host_msc usb freertos log vfs fatfs)
