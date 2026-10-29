#include "sd_storage.h"
#include "pin_config.h"
#include "driver/sdmmc_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include "esp_rom_gpio.h"
#include "soc/spi_periph.h"

static const char *TAG = "SD_STORAGE";

#define MOUNT_POINT_1 "/sd1"
#define MOUNT_POINT_2 "/sd2"

static bool sd1_ok = false;
static bool sd2_ok = false;

#define FREE_SPACE_THRESHOLD_BYTES (1024ULL * 1024ULL * 1024ULL)

#define FILE_INDEX_MIN 1
#define FILE_INDEX_MAX 9999

static FILE *s_flight_file_1 = NULL;
static FILE *s_flight_file_2 = NULL;

static uint32_t s_consec_err_1 = 0;
static uint32_t s_consec_err_2 = 0;

static sdmmc_card_t *s_card_1 = NULL;
static sdmmc_card_t *s_card_2 = NULL;

static SemaphoreHandle_t s_sd_mutex = NULL;

#ifdef PIN_SD_MISO2
static int s_miso_sel = 0;

static void init_miso_pads(void) {
    gpio_config_t conf = {
        .pin_bit_mask  = (1ULL << PIN_SD_MISO) | (1ULL << PIN_SD_MISO2),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_ENABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&conf);
}

static void switch_miso_to_sd1(void) {
    if (s_miso_sel == 1) return;
    esp_rom_gpio_connect_in_signal(PIN_SD_MISO,
                                   spi_periph_signal[SPI3_HOST].spiq_in,
                                   false);
    s_miso_sel = 1;
}

static void switch_miso_to_sd2(void) {
    if (s_miso_sel == 2) return;
    esp_rom_gpio_connect_in_signal(PIN_SD_MISO2,
                                   spi_periph_signal[SPI3_HOST].spiq_in,
                                   false);
    s_miso_sel = 2;
}
#else
#define init_miso_pads()     ((void)0)
#define switch_miso_to_sd1() ((void)0)
#define switch_miso_to_sd2() ((void)0)
#endif

static void scan_fdr_files_on_mount(const char* mount_point, bool exists[], int *count) {
  DIR *dir = opendir(mount_point);
  if (dir == NULL) {
    return;
  }

  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    if (strncmp(ent->d_name, "FDR_", 4) == 0) {
      int idx = 0;
      if (sscanf(ent->d_name, "FDR_%d", &idx) == 1) {
        if (idx >= FILE_INDEX_MIN && idx <= FILE_INDEX_MAX) {
          if (!exists[idx]) {
            exists[idx] = true;
            (*count)++;
          }
        }
      }
    }
  }
  closedir(dir);
}

static int get_next_file_index(const bool exists[], int count) {
  if (count == 0) return FILE_INDEX_MIN;
  for (int i = FILE_INDEX_MAX; i >= FILE_INDEX_MIN; i--) {
    if (exists[i]) {
      if (i == FILE_INDEX_MAX) {
        for (int j = FILE_INDEX_MIN; j < FILE_INDEX_MAX; j++) {
          if (!exists[j]) return j;
        }
        return -1;
      }
      return i + 1;
    }
  }
  return FILE_INDEX_MIN;
}

static int get_oldest_file_index_on_mount(const char* mount_point) {
  bool *exists = calloc(FILE_INDEX_MAX + 1, sizeof(bool));
  if (exists == NULL) return -1;
  int count = 0;
  scan_fdr_files_on_mount(mount_point, exists, &count);
  if (count == 0) { free(exists); return -1; }

  int result = FILE_INDEX_MIN;
  for (int i = FILE_INDEX_MIN; i < FILE_INDEX_MAX; i++) {
    if (!exists[i] && exists[i + 1]) {
      for (int j = i + 1; j <= FILE_INDEX_MAX; j++) {
        if (exists[j]) { result = j; goto done; }
      }
    }
  }
  for (int i = FILE_INDEX_MAX; i > FILE_INDEX_MIN; i--) {
    if (!exists[i] && exists[i - 1]) {
      for (int j = FILE_INDEX_MIN; j < i; j++) {
        if (exists[j]) { result = j; goto done; }
      }
    }
  }
done:
  free(exists);
  return result;
}

esp_err_t sd_storage_init(void) {
  ESP_LOGI(TAG, "Inicializando MicroSD (Dual SPI)...");
  s_sd_mutex = xSemaphoreCreateMutex();
  if (s_sd_mutex == NULL) return ESP_FAIL;

  // DRIVING CS PINS HIGH BEFORE SPI INIT TO PREVENT BUS COLLISION!
  gpio_set_direction(PIN_SD_CS1, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_SD_CS1, 1);
  
  gpio_set_direction(PIN_SD_CS2, GPIO_MODE_OUTPUT);
  gpio_set_level(PIN_SD_CS2, 1);
  
  vTaskDelay(pdMS_TO_TICKS(10));

#ifdef PIN_SD_MISO2
  init_miso_pads();
#endif

  spi_bus_config_t bus_cfg = {
      .mosi_io_num = PIN_SD_MOSI,
      .miso_io_num = PIN_SD_MISO,
      .sclk_io_num = PIN_SD_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 32 * 1024,
  };

  esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Fallo al inicializar bus SPI de las SD: %s", esp_err_to_name(ret));
      return ESP_FAIL;
  }

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 32 * 1024
  };

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI3_HOST;
  host.max_freq_khz = 10000;          // 10 MHz para estabilidad e integridad de senal
  host.command_timeout_ms = 15000;    // 15 s timeout para tolerar borrados NAND de FTL

  sdspi_device_config_t slot_config1 = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config1.gpio_cs = PIN_SD_CS1;
  slot_config1.host_id = SPI3_HOST;

  switch_miso_to_sd1();
  esp_err_t m1 = esp_vfs_fat_sdspi_mount(MOUNT_POINT_1, &host, &slot_config1, &mount_config, &s_card_1);
  if (m1 == ESP_OK) {
      sd1_ok = true;
      ESP_LOGI(TAG, "SD1 montada correctamente.");
  } else {
      ESP_LOGW(TAG, "Fallo al montar SD1 (%s). "
                    "SDXC de 32 GB+ suelen venir en exFAT; este firmware solo monta FAT32. "
                    "Formateá la tarjeta en FAT32 (cluster 32 KB) e insertala de nuevo.",
               esp_err_to_name(m1));
  }

  sdspi_device_config_t slot_config2 = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config2.gpio_cs = PIN_SD_CS2;
  slot_config2.host_id = SPI3_HOST;

  switch_miso_to_sd2();
  esp_err_t m2 = esp_vfs_fat_sdspi_mount(MOUNT_POINT_2, &host, &slot_config2, &mount_config, &s_card_2);
  if (m2 == ESP_OK) {
      sd2_ok = true;
      ESP_LOGI(TAG, "SD2 montada correctamente.");
  } else {
      ESP_LOGW(TAG, "Fallo al montar SD2 (%s). "
                    "Si es SDXC nueva, formateala en FAT32 (cluster 32 KB, no exFAT).",
               esp_err_to_name(m2));
  }

  if (sd1_ok || sd2_ok) return ESP_OK;
  return ESP_FAIL;
}

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

static esp_err_t open_file_on_mount(const char* mount_point, bool *sd_ok_flag, FILE **file_ptr, uint8_t cev_format) {
  if (!*sd_ok_flag) return ESP_FAIL;

  bool *exists = calloc(FILE_INDEX_MAX + 1, sizeof(bool));
  if (exists == NULL) return ESP_FAIL;
  int count = 0;
  scan_fdr_files_on_mount(mount_point, exists, &count);

  int next_idx = get_next_file_index(exists, count);
  free(exists);
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
    if (s_flight_file_1) { 
      switch_miso_to_sd1();
      fflush(s_flight_file_1);
      int fd = fileno(s_flight_file_1); if (fd >= 0) fsync(fd); 
      fclose(s_flight_file_1); s_flight_file_1 = NULL; 
    }
    if (s_flight_file_2) { 
      switch_miso_to_sd2();
      fflush(s_flight_file_2);
      int fd = fileno(s_flight_file_2); if (fd >= 0) fsync(fd); 
      fclose(s_flight_file_2); s_flight_file_2 = NULL; 
    }
  }

  sd_storage_ensure_free_space();

  switch_miso_to_sd1();
  esp_err_t r1 = open_file_on_mount(MOUNT_POINT_1, &sd1_ok, &s_flight_file_1, cev_format);
  if (s_flight_file_1 != NULL) {
    fflush(s_flight_file_1);
    fsync(fileno(s_flight_file_1));
  }

  switch_miso_to_sd2();
  esp_err_t r2 = open_file_on_mount(MOUNT_POINT_2, &sd2_ok, &s_flight_file_2, cev_format);
  if (s_flight_file_2 != NULL) {
    fflush(s_flight_file_2);
    fsync(fileno(s_flight_file_2));
  }

  s_consec_err_1 = 0;
  s_consec_err_2 = 0;

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  
  if (r1 == ESP_OK || r2 == ESP_OK) return ESP_OK;
  return ESP_FAIL;
}

esp_err_t sd_storage_write_dual(const void *data, size_t size) {
    if (s_flight_file_1 == NULL && s_flight_file_2 == NULL) return ESP_ERR_INVALID_STATE;
    if (data == NULL || size == 0) return ESP_ERR_INVALID_ARG;

    if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

    bool any_ok = false;

    if (s_flight_file_1 != NULL) {
        switch_miso_to_sd1();
        size_t written = fwrite(data, 1, size, s_flight_file_1);
        if (written != size) {
            s_consec_err_1++;
            ESP_LOGE(TAG, "Error de escritura en SD1 (%d/%d) [#%lu]",
                     (int)written, (int)size, (unsigned long)s_consec_err_1);
            gpio_set_level(PIN_SD_CS1, 1);
            clearerr(s_flight_file_1);
            if (s_consec_err_1 >= 3) {
                ESP_LOGE(TAG, "SD1 deshabilitada tras 3 fallos consecutivos. Continuando en SD2...");
                fclose(s_flight_file_1);
                s_flight_file_1 = NULL;
                sd1_ok = false;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            s_consec_err_1 = 0;
            any_ok = true;
        }
    }

    if (s_flight_file_2 != NULL) {
        switch_miso_to_sd2();
        size_t written = fwrite(data, 1, size, s_flight_file_2);
        if (written != size) {
            s_consec_err_2++;
            ESP_LOGE(TAG, "Error de escritura en SD2 (%d/%d) [#%lu]",
                     (int)written, (int)size, (unsigned long)s_consec_err_2);
            gpio_set_level(PIN_SD_CS2, 1);
            clearerr(s_flight_file_2);
            if (s_consec_err_2 >= 3) {
                ESP_LOGE(TAG, "SD2 deshabilitada tras 3 fallos consecutivos. Continuando en SD1...");
                fclose(s_flight_file_2);
                s_flight_file_2 = NULL;
                sd2_ok = false;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        } else {
            s_consec_err_2 = 0;
            any_ok = true;
        }
    }

    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return any_ok ? ESP_OK : ESP_FAIL;
}

esp_err_t sd_storage_write(const void *data, size_t length) {
    return sd_storage_write_dual(data, length);
}

esp_err_t sd_storage_flush_1(void) {
    if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    if (s_flight_file_1 != NULL) {
        switch_miso_to_sd1();
        int64_t t0 = esp_timer_get_time();
        fflush(s_flight_file_1);
        int fd = fileno(s_flight_file_1);
        if (fd >= 0) fsync(fd);
        uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
        ESP_LOGI(TAG, "fsync SD1 tardó %lu us", (unsigned long)us);
    }
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ESP_OK;
}

esp_err_t sd_storage_flush_2(void) {
    if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);
    if (s_flight_file_2 != NULL) {
        switch_miso_to_sd2();
        int64_t t0 = esp_timer_get_time();
        fflush(s_flight_file_2);
        int fd = fileno(s_flight_file_2);
        if (fd >= 0) fsync(fd);
        uint32_t us = (uint32_t)(esp_timer_get_time() - t0);
        ESP_LOGI(TAG, "fsync SD2 tardó %lu us", (unsigned long)us);
    }
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ESP_OK;
}

esp_err_t sd_storage_flush(void) {
    sd_storage_flush_1();
    sd_storage_flush_2();
    return ESP_OK;
}

esp_err_t sd_storage_write_error_log(const char *msg) {
  if (msg == NULL) return ESP_ERR_INVALID_ARG;

  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  const char *mounts[2] = {MOUNT_POINT_1, MOUNT_POINT_2};
  const bool ok_flags[2] = {sd1_ok, sd2_ok};
  int written = 0;

  for (int i = 0; i < 2; i++) {
    if (!ok_flags[i]) continue;

    char path[64];
    snprintf(path, sizeof(path), "%s/FDR_FULL.ERR", mounts[i]);

    FILE *f = fopen(path, "w");
    if (f == NULL) {
      ESP_LOGW(TAG, "No se pudo crear %s", path);
      continue;
    }
    fprintf(f, "%s\n", msg);
    fflush(f);
    int fd = fileno(f);
    if (fd >= 0) fsync(fd);
    fclose(f);
    written++;
    ESP_LOGI(TAG, "Log de error escrito en %s", path);
  }

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  return (written > 0) ? ESP_OK : ESP_FAIL;
}

void sd_storage_close(void) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  if (s_flight_file_1 != NULL) {
    switch_miso_to_sd1();
    fflush(s_flight_file_1);
    int fd = fileno(s_flight_file_1); if (fd >= 0) fsync(fd);
    fclose(s_flight_file_1); s_flight_file_1 = NULL;
  }
  if (s_flight_file_2 != NULL) {
    switch_miso_to_sd2();
    fflush(s_flight_file_2);
    int fd = fileno(s_flight_file_2); if (fd >= 0) fsync(fd);
    fclose(s_flight_file_2); s_flight_file_2 = NULL;
  }

  if (s_card_1 != NULL) { 
      esp_vfs_fat_sdcard_unmount(MOUNT_POINT_1, s_card_1); s_card_1 = NULL; 
  }
  if (s_card_2 != NULL) { 
      esp_vfs_fat_sdcard_unmount(MOUNT_POINT_2, s_card_2); s_card_2 = NULL; 
  }

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
}

esp_err_t sd_storage_format(void) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  if (s_flight_file_1 != NULL) {
    int fd = fileno(s_flight_file_1); if (fd >= 0) fsync(fd);
    fclose(s_flight_file_1); s_flight_file_1 = NULL;
  }
  if (s_flight_file_2 != NULL) {
    int fd = fileno(s_flight_file_2); if (fd >= 0) fsync(fd);
    fclose(s_flight_file_2); s_flight_file_2 = NULL;
  }

  esp_err_t r1 = ESP_OK, r2 = ESP_OK;
  if (s_card_1) {
    switch_miso_to_sd1();
    r1 = esp_vfs_fat_sdcard_format(MOUNT_POINT_1, s_card_1);
  }
  if (s_card_2) {
    switch_miso_to_sd2();
    r2 = esp_vfs_fat_sdcard_format(MOUNT_POINT_2, s_card_2);
  }

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  return (r1 == ESP_OK && r2 == ESP_OK) ? ESP_OK : ESP_FAIL;
}
