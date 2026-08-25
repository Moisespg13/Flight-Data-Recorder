import re

with open('original_sd_storage.c', 'r', encoding='utf-8') as f:
    c = f.read()

# 1. Mount points and variables
c = c.replace('#define MOUNT_POINT "/sdcard"', '#define MOUNT_POINT_1 "/sd1"\n#define MOUNT_POINT_2 "/sd2"\nstatic bool sd1_ok = false;\nstatic bool sd2_ok = false;')
c = c.replace('static FILE *s_flight_file = NULL;', 'static FILE *s_flight_file_1 = NULL;\nstatic FILE *s_flight_file_2 = NULL;')
c = c.replace('static sdmmc_card_t *s_card = NULL;', 'static sdmmc_card_t *s_card_1 = NULL;\nstatic sdmmc_card_t *s_card_2 = NULL;')

# 2. scan_fdr_files
scan_func = '''static void scan_fdr_files_on_mount(const char* mount_point, bool exists[], int *count) {
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
'''
c = re.sub(r'static void scan_fdr_files\(bool exists\[\], int \*count\) \{.*?(?=static int get_next_file_index)', scan_func, c, flags=re.DOTALL)

# 3. get_oldest_file_index - leave as is, but we need to run it per mount
oldest_repl = '''static int get_oldest_file_index_on_mount(const char* mount_point) {
  bool exists[FILE_INDEX_MAX + 1];
  int count = 0;
  memset(exists, 0, sizeof(bool) * (FILE_INDEX_MAX + 1));
  scan_fdr_files_on_mount(mount_point, exists, &count);
  if (count == 0) return -1;
  // (mismo algoritmo que get_oldest_file_index pero usando exists local)
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
'''
c = re.sub(r'static int get_oldest_file_index\(const bool exists\[\], int count\) \{.*?(?=// ============================================================================.*?// Inicialización)', oldest_repl, c, flags=re.DOTALL)

# 4. sd_storage_init
init_func = '''esp_err_t sd_storage_init(void) {
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

  // Montar SD1
  sdspi_device_config_t slot_config1 = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config1.gpio_cs = PIN_SD_CS1;
  slot_config1.host_id = SPI3_HOST;
  
  if (esp_vfs_fat_sdspi_mount(MOUNT_POINT_1, &host, &slot_config1, &mount_config, &s_card_1) == ESP_OK) {
      sd1_ok = true;
      ESP_LOGI(TAG, "SD1 montada correctamente.");
  } else {
      ESP_LOGW(TAG, "Fallo al montar SD1");
  }

  // Montar SD2
  sdspi_device_config_t slot_config2 = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config2.gpio_cs = PIN_SD_CS2;
  slot_config2.host_id = SPI3_HOST;

  if (esp_vfs_fat_sdspi_mount(MOUNT_POINT_2, &host, &slot_config2, &mount_config, &s_card_2) == ESP_OK) {
      sd2_ok = true;
      ESP_LOGI(TAG, "SD2 montada correctamente.");
  } else {
      ESP_LOGW(TAG, "Fallo al montar SD2");
  }

  if (sd1_ok || sd2_ok) return ESP_OK;
  return ESP_FAIL;
}
'''
c = re.sub(r'esp_err_t sd_storage_init\(void\) \{.*?(?=// ============================================================================.*?// Gestión de Espacio Libre)', init_func, c, flags=re.DOTALL)

# 5. sd_storage_ensure_free_space
ensure_space = '''
static esp_err_t ensure_free_space_on_mount(const char* mount_point, bool *sd_ok_flag) {
  if (!*sd_ok_flag) return ESP_OK;
  
  uint64_t total = 0, free_bytes = 0;
  if (esp_vfs_fat_info(mount_point, &total, &free_bytes) != ESP_OK) return ESP_FAIL;

  if (free_bytes >= FREE_SPACE_THRESHOLD_BYTES) return ESP_OK;
  
  ESP_LOGW(TAG, "Espacio bajo en %s. Liberando...", mount_point);
  while (free_bytes < FREE_SPACE_THRESHOLD_BYTES) {
    int oldest = get_oldest_file_index_on_mount(mount_point);
    if (oldest < 0) break;
    
    char file_to_delete[64];
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
'''
c = re.sub(r'esp_err_t sd_storage_ensure_free_space\(void\) \{.*?(?=// ============================================================================.*?// Apertura de archivo de vuelo)', ensure_space, c, flags=re.DOTALL)

with open('FDR_Master/components/sd_storage/sd_storage.c', 'w', encoding='utf-8') as f:
    f.write(c)

print("Done phase 1")
