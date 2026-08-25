import re

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

bad_fix = '''  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = 4000;
  
  if (host.init != NULL) {
      (*host.init)();
      host.init = NULL;
  }
  host.deinit = NULL;'''

orig = '''  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = 4000; // Bajamos la velocidad a 4 MHz para evitar errores de integridad de señal (0x108)'''

c = c.replace(bad_fix, orig)

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("Bad patch removed from prod.")
