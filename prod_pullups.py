import re

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

pullups = '''  // Habilitar Pull-ups internos por si los módulos no los tienen integrados
  gpio_pullup_en(PIN_SD_MOSI);
  gpio_pullup_en(PIN_SD_MISO);
  gpio_pullup_en(PIN_SD_CS1);
  gpio_pullup_en(PIN_SD_CS2);

  esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);'''

c = c.replace('esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);', pullups)

with open('C:/Users/yo/Documents/ESP/FDR/FDR_Master/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)
