
/**
 * @file sd_storage.c
 * @brief Implementación del almacenamiento en MicroSD vía SPI (Testbench).
 *
 * Utiliza el bus SPI3 del ESP32-S3 en modo SDSPI para comunicarse
 * con la tarjeta MicroSD. El sistema de archivos es FAT32, montado
 * automáticamente por el VFS de ESP-IDF.
 *
 * Estrategia de escritura:
 *   1. Los datos se acumulan en un buffer estático de 4 KB.
 *   2. Cuando el buffer se llena, se vuelca al archivo con fwrite().
 *   3. Cada N volcados, se llama a fsync() para forzar la escritura
 *      física a la tarjeta (protección ante cortes de energía).
 *
 * Gestión de espacio:
 *   - Al abrir un nuevo archivo de vuelo, se verifica que haya al
 *     menos 1 GB de espacio libre en la tarjeta.
 *   - Si no hay suficiente espacio, se borran los archivos FDR_XXXX.bin
 *     más antiguos (detección circular con wrap-around) hasta liberar
 *     el espacio requerido.
 *   - La numeración de archivos es estrictamente cronológica: el nuevo
 *     archivo siempre usa max_index + 1 (con wrap a 1 al pasar de 9999).
 */

#include "sd_storage.h"
#include "pin_config.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <dirent.h>
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

// Umbral mínimo de espacio libre para grabar (1 GB en bytes)
#define FREE_SPACE_THRESHOLD_BYTES (1024ULL * 1024ULL * 1024ULL)

// Rango de índices de archivo válidos
#define FILE_INDEX_MIN 1
#define FILE_INDEX_MAX 9999

// Buffer estático de escritura (evita malloc en runtime)
static uint8_t s_write_buf[WRITE_BUFFER_SIZE];
static size_t s_buf_pos = 0;

// Handle del archivo de vuelo abierto
static FILE *s_flight_file = NULL;

// Contador de flushes para decidir cuándo hacer fsync
static uint32_t s_flush_count = 0;

// Handle de la tarjeta SDMMC (se necesita para desmontar y formatear)
static sdmmc_card_t *s_card = NULL;

// Mutex para proteger acceso concurrente al archivo y a la SD
static SemaphoreHandle_t s_sd_mutex = NULL;

// ============================================================================
// Funciones internas: Gestión de archivos y espacio
// ============================================================================

/**
 * @brief Escanea el directorio /sdcard y marca qué índices FDR_XXXX.bin existen.
 *
 * Utiliza opendir/readdir para una sola pasada eficiente sobre el directorio,
 * en lugar de hacer stat() sobre 9999 nombres posibles.
 *
 * @param exists  Array de (FILE_INDEX_MAX + 1) booleanos. exists[i] = true si
 *                FDR_[i].bin existe en la tarjeta. El índice 0 no se usa.
 * @param count   Puntero donde se almacena la cantidad total de archivos encontrados.
 */
static void scan_fdr_files(bool exists[], int *count) {
  memset(exists, 0, sizeof(bool) * (FILE_INDEX_MAX + 1));
  *count = 0;

  DIR *dir = opendir(MOUNT_POINT);
  if (dir == NULL) {
    ESP_LOGE(TAG, "No se pudo abrir directorio %s", MOUNT_POINT);
    return;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    int idx = 0;
    
    // Parsear el número después de "FDR_" o "fdr_" ignorando el caso
    // Como sscanf es sensible a mayúsculas/minúsculas para literales,
    // es más seguro leer la parte entera y luego validar con strcasecmp.
    if (strncasecmp(entry->d_name, "FDR_", 4) == 0) {
      if (sscanf(entry->d_name + 4, "%d", &idx) == 1) {
        if (idx >= FILE_INDEX_MIN && idx <= FILE_INDEX_MAX) {
          // Verificar que el nombre sea exactamente "FDR_XXXX.bin" (ignorando mayúsculas/minúsculas)
          char expected[20];
          snprintf(expected, sizeof(expected), "FDR_%04d.bin", idx);
          
          if (strcasecmp(entry->d_name, expected) == 0) {
            exists[idx] = true;
            (*count)++;
          }
        }
      }
    }
  }

  closedir(dir);
  ESP_LOGI(TAG, "Escaneo SD: %d archivos FDR encontrados", *count);
}

/**
 * @brief Determina el siguiente índice de grabación manteniendo orden cronológico.
 *
 * Algoritmo:
 *   - Si no hay archivos: retorna 1.
 *   - Busca el índice máximo existente y retorna max + 1.
 *   - Si max == 9999, hace wrap-around a 1 y busca el primer slot libre
 *     desde 1 hacia adelante.
 *   - Si no hay slots libres, retorna -1 (error: tarjeta llena de índices).
 *
 * @param exists  Array de existencia generado por scan_fdr_files().
 * @param count   Cantidad de archivos encontrados.
 * @return Índice del siguiente archivo, o -1 si no hay espacio de numeración.
 */
static int get_next_file_index(const bool exists[], int count) {
  if (count == 0) {
    return FILE_INDEX_MIN; // Tarjeta vacía → FDR_0001.bin
  }

  // Encontrar el índice máximo existente
  int max_idx = 0;
  for (int i = FILE_INDEX_MAX; i >= FILE_INDEX_MIN; i--) {
    if (exists[i]) {
      max_idx = i;
      break;
    }
  }

  // Caso normal: max + 1
  if (max_idx < FILE_INDEX_MAX) {
    return max_idx + 1;
  }

  // Wrap-around: max es 9999, buscar el primer slot libre desde 1
  for (int i = FILE_INDEX_MIN; i <= FILE_INDEX_MAX; i++) {
    if (!exists[i]) {
      return i;
    }
  }

  // Todos los 9999 slots están ocupados (extremadamente raro)
  return -1;
}

/**
 * @brief Determina el índice del archivo más antiguo para borrado circular.
 *
 * Algoritmo con detección de wrap-around:
 *   - Busca un "gap" en la secuencia: una posición donde exists[i] == false
 *     y exists[i+1] == true. El archivo en i+1 es el inicio de la secuencia
 *     antigua (el más viejo).
 *   - Si no hay gap (todos consecutivos), el más viejo es simplemente el
 *     índice mínimo existente.
 *
 * Ejemplo de wrap-around:
 *   exists: [9998]=T [9999]=T [0001]=T [0002]=T
 *   El gap está entre 9999 y 0001 → no, están ambos true.
 *   El gap está entre 0002 y 0003 → exists[0003]=F, buscamos hacia atrás.
 *   Secuencia cronológica: 9998(viejo), 9999, 0001, 0002(nuevo)
 *   → El más viejo es 9998 (primer true después del gap que va desde 0003..9997)
 *
 * @param exists  Array de existencia generado por scan_fdr_files().
 * @param count   Cantidad de archivos encontrados.
 * @return Índice del archivo más antiguo, o -1 si no hay archivos.
 */
static int get_oldest_file_index(const bool exists[], int count) {
  if (count == 0) {
    return -1;
  }

  // Buscar un gap: una transición de false→true en el array circular.
  // El archivo justo después del gap (la primera posición true) es el más viejo.
  for (int i = FILE_INDEX_MIN; i <= FILE_INDEX_MAX; i++) {
    if (!exists[i]) {
      // Buscar el siguiente que sí exista (es el más viejo)
      for (int j = i + 1; j <= FILE_INDEX_MAX; j++) {
        if (exists[j]) {
          return j;
        }
      }
      // Si no encontramos nada después del gap, el más viejo está antes
      // (wrap-around: los archivos están solo al principio del rango)
      for (int j = FILE_INDEX_MIN; j < i; j++) {
        if (exists[j]) {
          return j;
        }
      }
    }
  }

  // Si no hay gaps (todos los slots de min a max están llenos), el más
  // viejo es simplemente el primero
  return FILE_INDEX_MIN;
}

// ============================================================================
// Inicialización
// ============================================================================

esp_err_t sd_storage_init(void) {
  ESP_LOGI(TAG, "Inicializando MicroSD (SDMMC 1-bit mode)...");

  s_sd_mutex = xSemaphoreCreateMutex();
  if (s_sd_mutex == NULL) {
    ESP_LOGE(TAG, "Error creando s_sd_mutex");
    return ESP_FAIL;
  }

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
      .format_if_mount_failed = false, // NO formatear si falla
      .max_files = 5,
      .allocation_unit_size = 16 * 1024,
  };

  // Configurar SDMMC Host (1-bit mode)
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT; // 20 MHz

  // Configurar Slot y Pines (1 línea de datos)
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;
  slot_config.clk = PIN_SD_CLK;
  slot_config.cmd = PIN_SD_CMD;
  slot_config.d0 = PIN_SD_D0;
  
  // Activar pull-ups internos
  slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  // Montar filesystem con sistema de reintentos (hasta 3 veces)
  ESP_LOGI(TAG, "Montando sistema de archivos...");
  
  esp_err_t ret = ESP_FAIL;
  int max_retries = 3;
  int retry_count = 0;
  
  while (retry_count < max_retries) {
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret == ESP_OK) {
        break;
    }
    retry_count++;
    ESP_LOGW(TAG, "Fallo al montar (Intento %d/%d). Reintentando en 500ms...", retry_count, max_retries);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      ESP_LOGE(TAG, "Fallo al montar tras %d intentos (formato incorrecto o sin tarjeta). Formatear a FAT32 en PC.", max_retries);
    } else {
      ESP_LOGE(TAG, "Error montando tarjeta tras %d intentos: %s", max_retries, esp_err_to_name(ret));
    }
    return ret;
  }

  sdmmc_card_print_info(stdout, s_card);
  ESP_LOGI(TAG, "MicroSD montada en %s", MOUNT_POINT);
  return ESP_OK;
}

// ============================================================================
// Gestión de espacio libre
// ============================================================================

esp_err_t sd_storage_ensure_free_space(void) {
  if (s_card == NULL) {
    ESP_LOGE(TAG, "MicroSD no montada, no se puede verificar espacio");
    return ESP_ERR_INVALID_STATE;
  }

  // Consultar espacio libre actual
  uint64_t total_bytes = 0, free_bytes = 0;
  esp_err_t ret = esp_vfs_fat_info(MOUNT_POINT, &total_bytes, &free_bytes);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al consultar espacio en SD: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "SD: Total=%llu MB, Libre=%llu MB, Umbral=%llu MB",
           (unsigned long long)(total_bytes / (1024 * 1024)),
           (unsigned long long)(free_bytes / (1024 * 1024)),
           (unsigned long long)(FREE_SPACE_THRESHOLD_BYTES / (1024 * 1024)));

  if (free_bytes >= FREE_SPACE_THRESHOLD_BYTES) {
    ESP_LOGI(TAG, "Espacio libre suficiente (%llu MB >= 1024 MB)",
             (unsigned long long)(free_bytes / (1024 * 1024)));
    return ESP_OK;
  }

  // No hay suficiente espacio: borrar archivos viejos
  ESP_LOGW(TAG, "Espacio insuficiente (%llu MB < 1024 MB). Liberando...",
           (unsigned long long)(free_bytes / (1024 * 1024)));

  // Escanear archivos existentes
  static bool file_exists[FILE_INDEX_MAX + 1];
  int file_count = 0;
  scan_fdr_files(file_exists, &file_count);

  int deleted = 0;
  while (free_bytes < FREE_SPACE_THRESHOLD_BYTES && file_count > 0) {
    int oldest = get_oldest_file_index(file_exists, file_count);
    if (oldest < 0) {
      ESP_LOGE(TAG, "No se encontró archivo para borrar");
      break;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), MOUNT_POINT "/FDR_%04d.bin", oldest);

    if (unlink(filepath) == 0) {
      ESP_LOGW(TAG, "Borrado: %s", filepath);
      file_exists[oldest] = false;
      file_count--;
      deleted++;
    } else {
      ESP_LOGE(TAG, "Error al borrar: %s", filepath);
      break;
    }

    // Re-consultar espacio libre
    ret = esp_vfs_fat_info(MOUNT_POINT, &total_bytes, &free_bytes);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error al re-consultar espacio: %s", esp_err_to_name(ret));
      return ret;
    }
  }

  if (free_bytes >= FREE_SPACE_THRESHOLD_BYTES) {
    ESP_LOGI(TAG, "Espacio liberado: %d archivos borrados. Libre ahora: %llu MB",
             deleted, (unsigned long long)(free_bytes / (1024 * 1024)));
    return ESP_OK;
  }

  ESP_LOGE(TAG, "No se pudo liberar suficiente espacio (libre: %llu MB)",
           (unsigned long long)(free_bytes / (1024 * 1024)));
  return ESP_FAIL;
}

// ============================================================================
// Apertura de archivo de vuelo
// ============================================================================

static esp_err_t flush_buffer(void);

esp_err_t sd_storage_open_flight_file(uint8_t cev_format) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  if (s_flight_file != NULL) {
    ESP_LOGW(TAG, "Ya hay un archivo de vuelo abierto. Cerrando anterior...");
    // Copiamos el cuerpo de sd_storage_close para evitar deadlock
    flush_buffer();
    int fd = fileno(s_flight_file);
    if (fd >= 0) fsync(fd);
    fclose(s_flight_file);
    s_flight_file = NULL;
  }

  // 1. Verificar y liberar espacio libre si es necesario
  esp_err_t ret = sd_storage_ensure_free_space();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "No hay espacio suficiente en la SD para grabar");
    return ret;
  }

  // 2. Escanear archivos existentes para determinar el siguiente índice
  static bool file_exists[FILE_INDEX_MAX + 1];
  int file_count = 0;
  scan_fdr_files(file_exists, &file_count);

  int next_idx = get_next_file_index(file_exists, file_count);
  if (next_idx < 0) {
    ESP_LOGE(TAG, "No hay índices de archivo disponibles (9999 archivos). Formateá la SD.");
    return ESP_FAIL;
  }

  // 3. Construir la ruta del archivo
  char filepath[64];
  snprintf(filepath, sizeof(filepath), MOUNT_POINT "/FDR_%04d.bin", next_idx);

  s_flight_file = fopen(filepath, "wb");
  if (s_flight_file == NULL) {
    ESP_LOGE(TAG, "Error al crear archivo: %s", filepath);
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
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
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Header FDR escrito (magic=0xFD464452, fmt=%d, crc=0x%08lX)",
           cev_format, (unsigned long)crc);

  // Resetear estado del buffer
  s_buf_pos = 0;
  s_flush_count = 0;

  ESP_LOGI(TAG, "Archivo de vuelo creado: %s", filepath);
  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  return ESP_OK;
}

// ============================================================================
// Escritura buffereada
// ============================================================================

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
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  if (s_flight_file == NULL) {
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ESP_ERR_INVALID_STATE;
  }
  if (data == NULL || length == 0) {
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
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
        if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
        return ret;
      }
    }
  }

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  return ESP_OK;
}

esp_err_t sd_storage_flush(void) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  esp_err_t ret = flush_buffer();
  if (ret != ESP_OK) {
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ret;
  }

  // Forzar fsync explícitamente
  if (s_flight_file != NULL) {
    int fd = fileno(s_flight_file);
    if (fd >= 0) {
      fsync(fd);
    }
  }

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  return ret;
}

// ============================================================================
// Cierre y desmontaje
// ============================================================================

void sd_storage_close(void) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

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

  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
}

// ============================================================================
// Formateo de la MicroSD
// ============================================================================

esp_err_t sd_storage_format(void) {
  if (s_sd_mutex) xSemaphoreTake(s_sd_mutex, portMAX_DELAY);

  if (s_card == NULL) {
    ESP_LOGE(TAG, "MicroSD no montada, no se puede formatear");
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ESP_ERR_INVALID_STATE;
  }

  // Si hay un archivo de vuelo abierto, cerrarlo primero (sin desmontar)
  if (s_flight_file != NULL) {
    flush_buffer();
    int fd = fileno(s_flight_file);
    if (fd >= 0) {
      fsync(fd);
    }
    fclose(s_flight_file);
    s_flight_file = NULL;
    ESP_LOGI(TAG, "Archivo de vuelo cerrado antes de formatear.");
  }

  ESP_LOGW(TAG, "*** FORMATEANDO MicroSD... Todos los datos serán borrados! ***");

  esp_err_t ret = esp_vfs_fat_sdcard_format(MOUNT_POINT, s_card);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al formatear MicroSD: %s", esp_err_to_name(ret));
    if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
    return ret;
  }

  ESP_LOGI(TAG, "MicroSD formateada exitosamente.");
  if (s_sd_mutex) xSemaphoreGive(s_sd_mutex);
  return ESP_OK;
}
