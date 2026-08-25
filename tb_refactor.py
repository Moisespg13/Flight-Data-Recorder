import re

with open('tb_original_sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Replace #include "driver/sdmmc_host.h"
c = c.replace('#include "driver/sdmmc_host.h"', '#include "driver/sdmmc_host.h"\n#include "driver/spi_master.h"')

# Modify sd_storage_init to use SPI3_HOST but for only ONE card (s_card, MOUNT_POINT, PIN_SD_CS1)
init_func = '''esp_err_t sd_storage_init(void) {
  ESP_LOGI(TAG, "Inicializando MicroSD por SPI (Single Card Test)...");
  s_sd_mutex = xSemaphoreCreateMutex();
  if (s_sd_mutex == NULL) {
    return ESP_FAIL;
  }

  spi_bus_config_t bus_cfg = {
      .mosi_io_num = PIN_SD_MOSI,
      .miso_io_num = PIN_SD_MISO,
      .sclk_io_num = PIN_SD_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 8192,
  };
  
  esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Fallo al inicializar bus SPI de la SD: %s", esp_err_to_name(ret));
      return ESP_FAIL;
  }

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 16 * 1024
  };

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = PIN_SD_CS1; // USING PIN_SD_CS1 for the single card
  slot_config.host_id = SPI3_HOST;

  ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Fallo al montar FATFS en MicroSD (SPI): %s", esp_err_to_name(ret));
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "MicroSD montada correctamente en %s via SPI.", MOUNT_POINT);
  return ESP_OK;
}
'''
c = re.sub(r'esp_err_t sd_storage_init\(void\) \{.*?(?=// ============================================================================.*?// Gestión de Espacio Libre)', init_func, c, flags=re.DOTALL)

with open('C:/Users/yo/Documents/ESP/FDR_TB/FDR_Master_TB/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("Testbench sd_storage_init updated to SPI.")
