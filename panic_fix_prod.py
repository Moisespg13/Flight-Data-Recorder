import re

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

fix = '''  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = 4000;
  
  if (host.init != NULL) {
      (*host.init)();
      host.init = NULL;
  }
  host.deinit = NULL;
'''

c = re.sub(r'  sdmmc_host_t host = SDSPI_HOST_DEFAULT\(\);\s*host\.slot = SPI3_HOST;\s*host\.max_freq_khz = 4000;.*?\n', fix + '\n', c)

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("Dual SD Panic workaround applied to prod.")
