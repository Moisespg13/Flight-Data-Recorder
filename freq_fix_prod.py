import re

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

c = c.replace('host.max_freq_khz = SDMMC_FREQ_DEFAULT;', 'host.max_freq_khz = 4000; // Bajamos la velocidad a 4 MHz para evitar errores de integridad de señal (0x108)')

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("SPI frequency reduced to 4MHz in prod.")
