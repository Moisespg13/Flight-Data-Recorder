import re

with open('original_sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

# Includes
c = c.replace('#include "driver/sdmmc_host.h"', '#include "driver/sdmmc_host.h"\n#include "driver/spi_master.h"')

# Replace MOUNT_POINT and state variables
c = c.replace('#define MOUNT_POINT "/sdcard"', '#define MOUNT_POINT_1 "/sd1"\n#define MOUNT_POINT_2 "/sd2"\nstatic bool sd1_ok = false;\nstatic bool sd2_ok = false;')
c = c.replace('static FILE *s_flight_file = NULL;', 'static FILE *s_flight_file_1 = NULL;\nstatic FILE *s_flight_file_2 = NULL;')
c = c.replace('static sdmmc_card_t *s_card = NULL;', 'static sdmmc_card_t *s_card_1 = NULL;\nstatic sdmmc_card_t *s_card_2 = NULL;')

# scan_fdr_files
c = re.sub(r'static void scan_fdr_files\(bool exists\[\], int \*count\) \{.*?(?=static int get_next_file_index)', 
'''static void scan_fdr_files_on_mount(const char* mount_point, bool exists[], int *count) {
  DIR *dir = opendir(mount_point);
  if (dir == NULL) return;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (strncmp(ent->d_name, "FDR_", 4) == 0 && strstr(ent->d_name, ".bin")) {
      int idx = 0;
      if (sscanf(ent->d_name, "FDR_%d.bin", &idx) == 1) {
        if (idx >= FILE_INDEX_MIN && idx <= FILE_INDEX_MAX) {
          if (!exists[idx]) { exists[idx] = true; (*count)++; }
        }
      }
    }
  }
  closedir(dir);
}

static void scan_fdr_files(bool exists[], int *count) {
  memset(exists, 0, sizeof(bool) * (FILE_INDEX_MAX + 1));
  *count = 0;
  if (sd1_ok) scan_fdr_files_on_mount(MOUNT_POINT_1, exists, count);
  if (sd2_ok) scan_fdr_files_on_mount(MOUNT_POINT_2, exists, count);
}
''', c, flags=re.DOTALL)

# get_oldest_file_index
c = re.sub(r'static int get_oldest_file_index\(const bool exists\[\], int count\) \{.*?(?=// ============================================================================.*?// Inicialización)', 
'''static int get_oldest_file_index_on_mount(const char* mount_point) {
  bool exists[FILE_INDEX_MAX + 1];
  int count = 0;
  memset(exists, 0, sizeof(bool) * (FILE_INDEX_MAX + 1));
  scan_fdr_files_on_mount(mount_point, exists, &count);
  if (count == 0) return -1;

  bool found_gap = false;
  for (int i = FILE_INDEX_MIN; i < FILE_INDEX_MAX; i++) {
    if (!exists[i] && exists[i + 1]) {
      for (int j = i + 1; j <= FILE_INDEX_MAX; j++) {
        if (exists[j]) return j;
      }
    }
  }
  for (int i = FILE_INDEX_MAX; i > FILE_INDEX_MIN; i--) {
    if (!exists[i] && exists[i - 1]) {
      for (int j = FILE_INDEX_MIN; j < i; j++) {
        if (exists[j]) return j;
      }
    }
  }
  return FILE_INDEX_MIN;
}
''', c, flags=re.DOTALL)

# sd_storage_init
c = re.sub(r'esp_err_t sd_storage_init\(void\) \{.*?(?=// ============================================================================.*?// Gestión de Espacio Libre)', 
'''esp_err_t sd_storage_init(void) {
  ESP_LOGI(TAG, "Inicializando MicroSD (Dual SPI)...");
  s_sd_mutex = xSemaphoreCreateMutex();
  if (s_sd_mutex == NULL) return ESP_FAIL;

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
      ESP_LOGE(TAG, "Fallo al inicializar bus SPI de las SD: %s", esp_err_to_name(ret));
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

  sdspi_device_config_t slot_config1 = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config1.gpio_cs = PIN_SD_CS1;
  slot_config1.host_id = SPI3_HOST;
  
  if (esp_vfs_fat_sdspi_mount(MOUNT_POINT_1, &host, &slot_config1, &mount_config, &s_card_1) == ESP_OK) {
      sd1_ok = true;
      ESP_LOGI(TAG, "SD1 montada correctamente.");
  }

  sdspi_device_config_t slot_config2 = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config2.gpio_cs = PIN_SD_CS2;
  slot_config2.host_id = SPI3_HOST;

  if (esp_vfs_fat_sdspi_mount(MOUNT_POINT_2, &host, &slot_config2, &mount_config, &s_card_2) == ESP_OK) {
      sd2_ok = true;
      ESP_LOGI(TAG, "SD2 montada correctamente.");
  }

  if (sd1_ok || sd2_ok) return ESP_OK;
  return ESP_FAIL;
}
''', c, flags=re.DOTALL)


# sd_storage_ensure_free_space
c = re.sub(r'esp_err_t sd_storage_ensure_free_space\(void\) \{.*?(?=// ============================================================================.*?// Apertura de archivo de vuelo)', 
'''
static esp_err_t ensure_free_space_on_mount(const char* mount_point, bool *sd_ok_flag) {
  if (!*sd_ok_flag) return ESP_OK;
  
  uint64_t total = 0, free_bytes = 0;
  if (esp_vfs_fat_info(mount_point, &total, &free_bytes) != ESP_OK) return ESP_FAIL;

  if (free_bytes >= FREE_SPACE_THRESHOLD_BYTES) return ESP_OK;
  
  ESP_LOGW(TAG, "Espacio bajo en %s. Liberando...", mount_point);
  while (free_bytes < FREE_SPACE_THRESHOLD_BYTES) {
    int oldest = get_oldest_file_index_on_mount(mount_point);
    if (oldest < 0) break;
    
    char file_to_delete[128];
    snprintf(file_to_delete, sizeof(file_to_delete), "%s/FDR_%04d.bin", mount_point, oldest);
    unlink(file_to_delete);
    
    if (esp_vfs_fat_info(mount_point, &total, &free_bytes) != ESP_OK) break;
  }
  return ESP_OK;
}

esp_err_t sd_storage_ensure_free_space(void) {
  esp_err_t r1 = ensure_free_space_on_mount(MOUNT_POINT_1, &sd1_ok);
  esp_err_t r2 = ensure_free_space_on_mount(MOUNT_POINT_2, &sd2_ok);
  if (r1 == ESP_OK || r2 == ESP_OK) return ESP_OK;
  return ESP_FAIL;
}
''', c, flags=re.DOTALL)


# sd_storage_open_flight_file
c = re.sub(r'static esp_err_t flush_buffer\(void\);.*?esp_err_t sd_storage_open_flight_file\(uint8_t cev_format\) \{.*?(?=// ============================================================================.*?// Escritura buffereada)',
'''static esp_err_t flush_buffer(void);

static esp_err_t open_file_on_mount(const char* mount_point, bool *sd_ok_flag, FILE **file_ptr, uint8_t cev_format) {
  if (!*sd_ok_flag) return ESP_FAIL;

  bool exists[FILE_INDEX_MAX + 1];
  int count = 0;
  memset(exists, 0, sizeof(bool) * (FILE_INDEX_MAX + 1));
  scan_fdr_files_on_mount(mount_point, exists, &count);

  int next_idx = get_next_file_index(exists, count);
  if (next_idx < 0) return ESP_FAIL;

  char filepath[128];
  snprintf(filepath, sizeof(filepath), "%s/FDR_%04d.bin", mount_point, next_idx);

  *file_ptr = fopen(filepath, "wb");
  if (*file_ptr == NULL) return ESP_FAIL;

  uint8_t hdr[12];
  hdr[0] = 0x52; hdr[1] = 0x44; hdr[2] = 0x46; hdr[3] = 0xFD;
  hdr[4] = 1; hdr[5] = cev_format; hdr[6] = 0; hdr[7] = 0;

  uint32_t crc = 0xFFFFFFFF;
  for (int i = 0; i < 8; i++) {
    crc ^= hdr[i];
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
      else crc >>= 1;
    }
  }
  crc ^= 0xFFFFFFFF;

  hdr[8]  = (uint8_t)(crc);
  hdr[9]  = (uint8_t)(crc >> 8);
  hdr[10] = (uint8_t)(crc >> 16);
  hdr[11] = (uint8_t)(crc >> 24);

  size_t written = fwrite(hdr, 1, sizeof(hdr), *file_ptr);
  if (written != sizeof(hdr)) {
    fclose(*file_ptr); *file_ptr = NULL; *sd_ok_flag = false;
    return ESP_FAIL;
  }
  return ESP_OK;
}

esp_err_t sd_storage_open_flight_file(uint8_t cev_format) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  if (s_flight_file_1 != NULL || s_flight_file_2 != NULL) {
    flush_buffer();
    if (s_flight_file_1) { int fd = fileno(s_flight_file_1); if (fd >= 0) fsync(fd); fclose(s_flight_file_1); s_flight_file_1 = NULL; }
    if (s_flight_file_2) { int fd = fileno(s_flight_file_2); if (fd >= 0) fsync(fd); fclose(s_flight_file_2); s_flight_file_2 = NULL; }
  }

  sd_storage_ensure_free_space();

  esp_err_t r1 = open_file_on_mount(MOUNT_POINT_1, &sd1_ok, &s_flight_file_1, cev_format);
  esp_err_t r2 = open_file_on_mount(MOUNT_POINT_2, &sd2_ok, &s_flight_file_2, cev_format);

  s_buf_pos = 0;
  s_flush_count = 0;

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  
  if (r1 == ESP_OK || r2 == ESP_OK) return ESP_OK;
  return ESP_FAIL;
}
''', c, flags=re.DOTALL)


# flush_buffer
c = re.sub(r'static esp_err_t flush_buffer\(void\) \{.*?(?=esp_err_t sd_storage_write)', 
'''static esp_err_t flush_buffer(void) {
  if (s_buf_pos == 0) return ESP_OK;

  esp_err_t ret = ESP_FAIL;

  if (s_flight_file_1 != NULL) {
    size_t written = fwrite(s_write_buf, 1, s_buf_pos, s_flight_file_1);
    if (written != s_buf_pos) {
      ESP_LOGE(TAG, "Error escritura SD1");
      fclose(s_flight_file_1); s_flight_file_1 = NULL; sd1_ok = false;
    } else {
      ret = ESP_OK;
      if ((s_flush_count % FSYNC_EVERY_N_FLUSHES) == 0) {
        int fd = fileno(s_flight_file_1);
        if (fd >= 0) fsync(fd);
      }
    }
  }

  if (s_flight_file_2 != NULL) {
    size_t written = fwrite(s_write_buf, 1, s_buf_pos, s_flight_file_2);
    if (written != s_buf_pos) {
      ESP_LOGE(TAG, "Error escritura SD2");
      fclose(s_flight_file_2); s_flight_file_2 = NULL; sd2_ok = false;
    } else {
      ret = ESP_OK;
      if ((s_flush_count % FSYNC_EVERY_N_FLUSHES) == 0) {
        int fd = fileno(s_flight_file_2);
        if (fd >= 0) fsync(fd);
      }
    }
  }

  s_buf_pos = 0;
  s_flush_count++;

  return ret;
}

''', c, flags=re.DOTALL)


# sd_storage_write
c = c.replace('''esp_err_t sd_storage_write(const void *data, size_t length) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  if (s_flight_file == NULL) {''', 
'''esp_err_t sd_storage_write(const void *data, size_t length) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  if (s_flight_file_1 == NULL && s_flight_file_2 == NULL) {''')

# sd_storage_flush
c = re.sub(r'esp_err_t sd_storage_flush\(void\) \{.*?(?=// ============================================================================.*?// Cierre y desmontaje)', 
'''esp_err_t sd_storage_flush(void) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  esp_err_t ret = flush_buffer();

  if (s_flight_file_1 != NULL) {
    int fd = fileno(s_flight_file_1);
    if (fd >= 0) fsync(fd);
  }
  if (s_flight_file_2 != NULL) {
    int fd = fileno(s_flight_file_2);
    if (fd >= 0) fsync(fd);
  }

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  return ret;
}
''', c, flags=re.DOTALL)

# sd_storage_close
c = re.sub(r'void sd_storage_close\(void\) \{.*?(?=// ============================================================================.*?// Formateo)', 
'''void sd_storage_close(void) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  flush_buffer();
  if (s_flight_file_1 != NULL) {
    int fd = fileno(s_flight_file_1); if (fd >= 0) fsync(fd);
    fclose(s_flight_file_1); s_flight_file_1 = NULL;
  }
  if (s_flight_file_2 != NULL) {
    int fd = fileno(s_flight_file_2); if (fd >= 0) fsync(fd);
    fclose(s_flight_file_2); s_flight_file_2 = NULL;
  }

  if (s_card_1 != NULL) { esp_vfs_fat_sdcard_unmount(MOUNT_POINT_1, s_card_1); s_card_1 = NULL; }
  if (s_card_2 != NULL) { esp_vfs_fat_sdcard_unmount(MOUNT_POINT_2, s_card_2); s_card_2 = NULL; }

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
}
''', c, flags=re.DOTALL)

# sd_storage_format
c = re.sub(r'esp_err_t sd_storage_format\(void\) \{.*', 
'''esp_err_t sd_storage_format(void) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  flush_buffer();
  if (s_flight_file_1 != NULL) {
    int fd = fileno(s_flight_file_1); if (fd >= 0) fsync(fd);
    fclose(s_flight_file_1); s_flight_file_1 = NULL;
  }
  if (s_flight_file_2 != NULL) {
    int fd = fileno(s_flight_file_2); if (fd >= 0) fsync(fd);
    fclose(s_flight_file_2); s_flight_file_2 = NULL;
  }

  esp_err_t r1 = ESP_OK, r2 = ESP_OK;
  if (s_card_1) r1 = esp_vfs_fat_sdcard_format(MOUNT_POINT_1, s_card_1);
  if (s_card_2) r2 = esp_vfs_fat_sdcard_format(MOUNT_POINT_2, s_card_2);

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  return (r1 == ESP_OK && r2 == ESP_OK) ? ESP_OK : ESP_FAIL;
}
''', c, flags=re.DOTALL)

with open('FDR_Master/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("Refactor complete")
