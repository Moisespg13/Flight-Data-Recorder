
/**
 * @file sd_storage.c
 * @brief Implementación del almacenamiento en MicroSD vía SDMMC.
 *
 * Utiliza el periférico SDMMC del ESP32-S3 en modo 1-bit para
 * comunicarse con la tarjeta MicroSD. El sistema de archivos es
 * FAT32, montado automáticamente por el VFS de ESP-IDF.
 *
 * Estrategia de escritura:
 *   1. Los datos se acumulan en un buffer estático de 4 KB.
 *   2. Cuando el buffer se llena, se vuelca al archivo con fwrite().
 *   3. Cada N volcados, se llama a fsync() para forzar la escritura
 *      física a la tarjeta (protección ante cortes de energía).
 */

#include "sd_storage.h"
#include "pin_config.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

static const char *TAG = "SD_STORAGE";

// Punto de montaje del sistema de archivos
#define MOUNT_POINT "/sdcard"

// Tamaño del buffer de escritura (4 KB = tamaño típico de sector)
#define WRITE_BUFFER_SIZE 4096

// Cada cuántos flushes de buffer hacemos un fsync() al disco
// (fsync es costoso; hacerlo cada vez mataría el rendimiento)
#define FSYNC_EVERY_N_FLUSHES 8

// Buffer estático de escritura (evita malloc en runtime)
static uint8_t s_write_buf[WRITE_BUFFER_SIZE];
static size_t s_buf_pos = 0;

// Handle del archivo de vuelo abierto
static FILE *s_flight_file = NULL;

// Contador de flushes para decidir cuándo hacer fsync
static uint32_t s_flush_count = 0;

// Handle de la tarjeta SDMMC (se necesita para desmontar)
static sdmmc_card_t *s_card = NULL;

esp_err_t sd_storage_init(void) {
  ESP_LOGI(TAG, "Inicializando almacenamiento MicroSD...");

  // Opciones de montaje del sistema de archivos FAT
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false, // NO formatear si falla (seguridad)
      .max_files = 5,
      .allocation_unit_size = 16 * 1024, // Tamaño de cluster (16 KB)
  };

  // Configuración del host SDMMC
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED; // 40 MHz

  // Configuración del slot SDMMC (modo 1-bit: solo usa D0)
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1; // Modo 1-bit (CMD + CLK + D0)
  slot_config.cmd = PIN_SD_CMD;
  slot_config.clk = PIN_SD_CLK;
  slot_config.d0 = PIN_SD_D0;
  // Habilitar pull-ups internos (necesarios para la señalización SD)
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  // Montar el sistema de archivos
  esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config,
                                           &mount_config, &s_card);
  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG,
               "No se pudo montar el filesystem. "
               "Si la tarjeta es nueva, formateala como FAT32 en una PC.");
    } else {
      ESP_LOGE(TAG, "Error al inicializar la tarjeta SD: %s",
               esp_err_to_name(ret));
    }
    return ret;
  }

  // Mostrar información de la tarjeta detectada
  sdmmc_card_print_info(stdout, s_card);

  ESP_LOGI(TAG, "MicroSD montada en %s", MOUNT_POINT);
  return ESP_OK;
}

esp_err_t sd_storage_open_flight_file(uint8_t cev_format) {
  if (s_flight_file != NULL) {
    ESP_LOGW(TAG, "Ya hay un archivo de vuelo abierto. Cerrando anterior...");
    sd_storage_close();
  }

  // Buscar el siguiente número de archivo disponible
  char filepath[64];
  struct stat st;

  for (int i = 1; i <= 9999; i++) {
    snprintf(filepath, sizeof(filepath), MOUNT_POINT "/FDR_%04d.bin", i);
    if (stat(filepath, &st) != 0) {
      // El archivo no existe: podemos usarlo
      break;
    }
    if (i == 9999) {
      ESP_LOGE(TAG, "Se llegó al límite de archivos (9999). Borrá los viejos.");
      return ESP_FAIL;
    }
  }

  s_flight_file = fopen(filepath, "wb");
  if (s_flight_file == NULL) {
    ESP_LOGE(TAG, "Error al crear archivo: %s", filepath);
    return ESP_FAIL;
  }

  // ── Escribir header FDR (12 bytes) ──────────────────────────────
  // Estructura (Little-Endian):
  //   [0..3]  magic    = 0xFD464452  ("FDR" + 0xFD)
  //   [4]     version  = 1
  //   [5]     format   = 1, 2 o 3   (formato CEV activo)
  //   [6..7]  reserved = 0x0000
  //   [8..11] crc32    = CRC32 de los bytes [0..7]
  uint8_t hdr[12];

  // Magic (Little-Endian)
  hdr[0] = 0x52; // 'R'
  hdr[1] = 0x44; // 'D'
  hdr[2] = 0x46; // 'F'
  hdr[3] = 0xFD;

  // Version + Format + Reserved
  hdr[4] = 1;           // version
  hdr[5] = cev_format;  // formato CEV
  hdr[6] = 0;           // reserved
  hdr[7] = 0;           // reserved

  // CRC32 de los primeros 8 bytes (compatible con Python binascii.crc32)
  uint32_t crc = 0xFFFFFFFF;
  for (int i = 0; i < 8; i++) {
    crc ^= hdr[i];
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xEDB88320;
      else
        crc >>= 1;
    }
  }
  crc ^= 0xFFFFFFFF;

  // Escribir CRC en Little-Endian
  hdr[8]  = (uint8_t)(crc);
  hdr[9]  = (uint8_t)(crc >> 8);
  hdr[10] = (uint8_t)(crc >> 16);
  hdr[11] = (uint8_t)(crc >> 24);

  size_t written = fwrite(hdr, 1, sizeof(hdr), s_flight_file);
  if (written != sizeof(hdr)) {
    ESP_LOGE(TAG, "Error al escribir header FDR");
    fclose(s_flight_file);
    s_flight_file = NULL;
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Header FDR escrito (magic=0xFD464452, fmt=%d, crc=0x%08lX)",
           cev_format, (unsigned long)crc);

  // Resetear estado del buffer
  s_buf_pos = 0;
  s_flush_count = 0;

  ESP_LOGI(TAG, "Archivo de vuelo creado: %s", filepath);
  return ESP_OK;
}

/**
 * @brief Vuelca el contenido del buffer interno al archivo en disco.
 */
static esp_err_t flush_buffer(void) {
  if (s_flight_file == NULL || s_buf_pos == 0) {
    return ESP_OK; // Nada que hacer
  }

  size_t written = fwrite(s_write_buf, 1, s_buf_pos, s_flight_file);
  if (written != s_buf_pos) {
    ESP_LOGE(TAG, "Error de escritura: esperado %u, escrito %u",
             (unsigned)s_buf_pos, (unsigned)written);
    return ESP_FAIL;
  }

  s_buf_pos = 0;
  s_flush_count++;

  // Hacer fsync periódicamente para proteger ante cortes de energía
  if ((s_flush_count % FSYNC_EVERY_N_FLUSHES) == 0) {
    int fd = fileno(s_flight_file);
    if (fd >= 0) {
      fsync(fd);
    }
  }

  return ESP_OK;
}

esp_err_t sd_storage_write(const void *data, size_t length) {
  if (s_flight_file == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (data == NULL || length == 0) {
    return ESP_ERR_INVALID_ARG;
  }

  const uint8_t *src = (const uint8_t *)data;
  size_t remaining = length;

  while (remaining > 0) {
    // ¿Cuánto espacio libre hay en el buffer?
    size_t space = WRITE_BUFFER_SIZE - s_buf_pos;
    size_t to_copy = (remaining < space) ? remaining : space;

    memcpy(s_write_buf + s_buf_pos, src, to_copy);
    s_buf_pos += to_copy;
    src += to_copy;
    remaining -= to_copy;

    // Si el buffer se llenó, volcarlo al archivo
    if (s_buf_pos >= WRITE_BUFFER_SIZE) {
      esp_err_t ret = flush_buffer();
      if (ret != ESP_OK) {
        return ret;
      }
    }
  }

  return ESP_OK;
}

esp_err_t sd_storage_flush(void) {
  esp_err_t ret = flush_buffer();
  if (ret != ESP_OK) {
    return ret;
  }

  // Forzar fsync explícitamente
  if (s_flight_file != NULL) {
    int fd = fileno(s_flight_file);
    if (fd >= 0) {
      fsync(fd);
    }
  }

  return ret;
}

void sd_storage_close(void) {
  if (s_flight_file != NULL) {
    // Vaciar lo que quede en el buffer
    flush_buffer();

    // fsync final
    int fd = fileno(s_flight_file);
    if (fd >= 0) {
      fsync(fd);
    }

    fclose(s_flight_file);
    s_flight_file = NULL;
    ESP_LOGI(TAG, "Archivo de vuelo cerrado.");
  }

  // Desmontar la tarjeta
  if (s_card != NULL) {
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    ESP_LOGI(TAG, "MicroSD desmontada.");
  }
}
