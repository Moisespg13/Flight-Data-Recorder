import re

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/sd_storage/sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

bad_fix = '''  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = 4000;
  
  // WORKAROUND ESP-IDF: Inicializamos el driver SDSPI manualmente una vez.
  // Luego anulamos los punteros init/deinit en el struct para evitar que 
  // esp_vfs_fat_sdspi_mount destruya el driver si el montaje de una SD falla,
  // lo cual corrompería a la otra SD ya montada (causando StoreProhibited).
  if (host.init != NULL) {
      (*host.init)();
      host.init = NULL;
  }
  host.deinit = NULL;'''

orig = '''  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = 4000; // Bajamos la velocidad a 4 MHz para evitar errores de integridad de señal (0x108) en protoboards/cables sueltos'''

c = c.replace(bad_fix, orig)

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("Bad patch removed.")
