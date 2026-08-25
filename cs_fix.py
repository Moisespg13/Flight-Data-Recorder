import re

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/sd_storage/sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Replace the pullup logic with strong pull-ups (driving them high)
old_pullups = '''  // Habilitar Pull-ups internos por si los módulos no los tienen integrados
  gpio_pullup_en(PIN_SD_MOSI);
  gpio_pullup_en(PIN_SD_MISO);
  gpio_pullup_en(PIN_SD_CS1);
  gpio_pullup_en(PIN_SD_CS2);

  esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);'''

new_pullups = '''  // Pull-ups para datos
  gpio_pullup_en(PIN_SD_MOSI);
  gpio_pullup_en(PIN_SD_MISO);
  
  // DRIVING CS PINS HIGH BEFORE SPI INIT TO PREVENT BUS COLLISION!
  // Los módulos de SD genéricos suelen tener pull-downs que le ganan al pull-up interno, 
  // dejando el CS en LOW (activo). Si ambos módulos están activos, colisionan en el bus MISO (Error 0x108).
  gpio_set_direction(PIN_SD_CS1, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_SD_CS1, 1);
  
  gpio_set_direction(PIN_SD_CS2, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_SD_CS2, 1);
  
  // Un pequeño delay para que las señales se asienten
  vTaskDelay(pdMS_TO_TICKS(10));

  esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);'''

c = c.replace(old_pullups, new_pullups)

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("CS driving added.")
