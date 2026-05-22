
/**
 * @file usb_transfer.c
 * @brief Transferencia de archivos de vuelo vía USB Host (pendrive).
 *
 * Implementa el modo de descarga del MAD. Utiliza el stack USB Host
 * del ESP32-S3 con el driver de clase MSC (Mass Storage Class) para
 * montar un pendrive como sistema de archivos FAT32 y copiar los
 * archivos .bin de la MicroSD al pendrive.
 *
 * El ESP32-S3 tiene un controlador USB-OTG que puede operar en
 * modo Host (como una PC) o Device (como un pendrive). Aquí
 * lo usamos en modo Host.
 *
 * Arquitectura interna:
 *   - usb_host_lib_task:  Tarea del stack USB (maneja eventos del bus)
 *   - MSC class driver:   Detecta y monta dispositivos de almacenamiento
 *   - VFS + FATFS:        Monta el pendrive como /usb para usar fopen/fwrite
 */

#include "usb_transfer.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/msc_host.h"
#include "usb/msc_host_vfs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "USB_TRANSFER";

// Rutas de montaje
#define SD_MOUNT_POINT  "/sdcard"
#define USB_MOUNT_POINT "/usb"

// Buffer para copia de archivos (8 KB)
#define COPY_BUF_SIZE 8192

// Estado del módulo
static bool s_usb_ready = false;
static bool s_initialized = false;

// Handles USB
static TaskHandle_t s_usb_task_handle = NULL;
static msc_host_device_handle_t s_msc_device = NULL;
static msc_host_vfs_handle_t s_vfs_handle = NULL;

// Cola de eventos de la aplicación
static QueueHandle_t s_app_queue = NULL;

// Mensajes internos
typedef struct {
  enum {
    USB_EVT_CONNECTED,
    USB_EVT_DISCONNECTED,
  } id;
  union {
    uint8_t dev_address;
    msc_host_device_handle_t device_handle;
  } data;
} usb_app_msg_t;

// ============================================================================
// Callback del driver MSC (llamado desde contexto USB)
// ============================================================================
static void msc_event_cb(const msc_host_event_t *event, void *arg) {
  usb_app_msg_t msg;

  if (event->event == MSC_DEVICE_CONNECTED) {
    ESP_LOGI(TAG, "Pendrive conectado (USB addr=%d)", event->device.address);
    msg.id = USB_EVT_CONNECTED;
    msg.data.dev_address = event->device.address;
    xQueueSend(s_app_queue, &msg, portMAX_DELAY);

  } else if (event->event == MSC_DEVICE_DISCONNECTED) {
    ESP_LOGI(TAG, "Pendrive desconectado");
    msg.id = USB_EVT_DISCONNECTED;
    msg.data.device_handle = event->device.handle;
    xQueueSend(s_app_queue, &msg, portMAX_DELAY);
  }
}

// ============================================================================
// Tarea interna del stack USB Host
// ============================================================================
/**
 * @brief Tarea que procesa los eventos del bus USB.
 *
 * El stack USB Host de ESP-IDF requiere una tarea dedicada que
 * llame a usb_host_lib_handle_events() continuamente para
 * procesar conexiones, desconexiones y transferencias.
 */
static void usb_host_lib_task(void *pvParameters) {
  while (1) {
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);

    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      ESP_LOGI(TAG, "Todos los clientes USB desconectados");
      // Liberar dispositivos huérfanos
      usb_host_device_free_all();
    }
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
      ESP_LOGI(TAG, "Todos los dispositivos USB liberados");
    }
  }
}

// ============================================================================
// Montaje/desmontaje del pendrive
// ============================================================================

/**
 * @brief Monta el pendrive conectado como filesystem en /usb.
 */
static esp_err_t mount_pendrive(uint8_t dev_address) {
  // Instalar el dispositivo MSC
  esp_err_t ret = msc_host_install_device(dev_address, &s_msc_device);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al instalar dispositivo MSC: %s", esp_err_to_name(ret));
    return ret;
  }

  // Mostrar información del pendrive
  msc_host_device_info_t info;
  ret = msc_host_get_device_info(s_msc_device, &info);
  if (ret == ESP_OK) {
    uint64_t capacity_mb =
        ((uint64_t)info.sector_size * info.sector_count) / (1024 * 1024);
    ESP_LOGI(TAG, "Pendrive: %llu MB, sector=%"PRIu32", PID=0x%04X, VID=0x%04X",
             capacity_mb, info.sector_size, info.idProduct, info.idVendor);
  }

  // Montar como filesystem FAT en /usb
  const esp_vfs_fat_mount_config_t mount_config = {
      .format_if_mount_failed = false,
      .max_files = 5,
      .allocation_unit_size = 8192,
  };

  ret = msc_host_vfs_register(s_msc_device, USB_MOUNT_POINT,
                              &mount_config, &s_vfs_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al montar pendrive en %s: %s",
             USB_MOUNT_POINT, esp_err_to_name(ret));
    msc_host_uninstall_device(s_msc_device);
    s_msc_device = NULL;
    return ret;
  }

  ESP_LOGI(TAG, "Pendrive montado en %s", USB_MOUNT_POINT);
  s_usb_ready = true;
  return ESP_OK;
}

/**
 * @brief Desmonta el pendrive y libera recursos.
 */
static void unmount_pendrive(void) {
  s_usb_ready = false;

  if (s_vfs_handle != NULL) {
    msc_host_vfs_unregister(s_vfs_handle);
    s_vfs_handle = NULL;
  }
  if (s_msc_device != NULL) {
    msc_host_uninstall_device(s_msc_device);
    s_msc_device = NULL;
  }

  ESP_LOGI(TAG, "Pendrive desmontado");
}

// ============================================================================
// Copia de archivos
// ============================================================================

/**
 * @brief Copia un archivo individual de origen a destino.
 */
static esp_err_t copy_file(const char *src_path, const char *dst_path) {
  FILE *src = fopen(src_path, "rb");
  if (src == NULL) {
    ESP_LOGE(TAG, "No se puede abrir origen: %s", src_path);
    return ESP_ERR_NOT_FOUND;
  }

  FILE *dst = fopen(dst_path, "wb");
  if (dst == NULL) {
    ESP_LOGE(TAG, "No se puede crear destino: %s", dst_path);
    fclose(src);
    return ESP_FAIL;
  }

  // Obtener tamaño del archivo
  fseek(src, 0, SEEK_END);
  long file_size = ftell(src);
  fseek(src, 0, SEEK_SET);

  ESP_LOGI(TAG, "Copiando %s (%ld bytes)...", src_path, file_size);

  uint8_t *buf = malloc(COPY_BUF_SIZE);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Sin memoria para buffer de copia");
    fclose(src);
    fclose(dst);
    return ESP_ERR_NO_MEM;
  }

  // Usar buffer grande para escrituras más eficientes al USB
  setvbuf(dst, NULL, _IOFBF, COPY_BUF_SIZE);

  long total_copied = 0;
  size_t bytes_read;

  while ((bytes_read = fread(buf, 1, COPY_BUF_SIZE, src)) > 0) {
    size_t bytes_written = fwrite(buf, 1, bytes_read, dst);
    if (bytes_written != bytes_read) {
      ESP_LOGE(TAG, "Error de escritura en pendrive (espacio insuficiente?)");
      free(buf);
      fclose(src);
      fclose(dst);
      return ESP_FAIL;
    }

    total_copied += bytes_written;

    // Progreso cada 64 KB
    if (file_size > 0 && (total_copied % (64 * 1024)) == 0) {
      int percent = (int)((total_copied * 100) / file_size);
      ESP_LOGI(TAG, "  Progreso: %d%% (%ld / %ld bytes)", percent,
               total_copied, file_size);
    }
  }

  free(buf);
  fclose(src);
  fclose(dst);

  ESP_LOGI(TAG, "OK: %s copiado (%ld bytes)", dst_path, total_copied);
  return ESP_OK;
}

// ============================================================================
// API Pública
// ============================================================================

esp_err_t usb_transfer_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Inicializando USB Host...");

  // Crear cola de eventos
  s_app_queue = xQueueCreate(5, sizeof(usb_app_msg_t));
  if (s_app_queue == NULL) {
    return ESP_ERR_NO_MEM;
  }

  // Instalar el stack USB Host
  usb_host_config_t host_config = {
      .skip_phy_setup = false,
      .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  esp_err_t ret = usb_host_install(&host_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al instalar USB Host: %s", esp_err_to_name(ret));
    vQueueDelete(s_app_queue);
    s_app_queue = NULL;
    return ret;
  }

  // Crear tarea de procesamiento de eventos USB
  BaseType_t ok = xTaskCreate(usb_host_lib_task, "usb_host", 4096, NULL, 2,
                              &s_usb_task_handle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea USB Host");
    usb_host_uninstall();
    vQueueDelete(s_app_queue);
    s_app_queue = NULL;
    return ESP_ERR_NO_MEM;
  }

  // Instalar el driver de clase MSC con callback de eventos
  const msc_host_driver_config_t msc_config = {
      .create_backround_task = true,
      .task_priority = 5,
      .stack_size = 4096,
      .callback = msc_event_cb,
  };

  ret = msc_host_install(&msc_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al instalar driver MSC: %s", esp_err_to_name(ret));
    vTaskDelete(s_usb_task_handle);
    s_usb_task_handle = NULL;
    usb_host_uninstall();
    vQueueDelete(s_app_queue);
    s_app_queue = NULL;
    return ret;
  }

  s_initialized = true;
  ESP_LOGI(TAG, "USB Host + MSC driver inicializados. Esperando pendrive...");

  // Procesar eventos pendientes: si ya hay un pendrive conectado,
  // el callback de MSC lo notificará vía la cola.
  usb_app_msg_t msg;
  if (xQueueReceive(s_app_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (msg.id == USB_EVT_CONNECTED) {
      mount_pendrive(msg.data.dev_address);
    }
  }

  return ESP_OK;
}

esp_err_t usb_transfer_copy_all(void) {
  if (!s_initialized || !s_usb_ready) {
    ESP_LOGE(TAG, "USB no inicializado o pendrive no montado");
    return ESP_ERR_INVALID_STATE;
  }

  // Abrir directorio de la SD
  DIR *dir = opendir(SD_MOUNT_POINT);
  if (dir == NULL) {
    ESP_LOGE(TAG, "No se puede abrir %s", SD_MOUNT_POINT);
    return ESP_ERR_NOT_FOUND;
  }

  int files_copied = 0;
  int errors = 0;
  struct dirent *entry;

  while ((entry = readdir(dir)) != NULL) {
    // Buscar archivos que empiecen con "FDR_" y terminen en ".bin"
    if (strncmp(entry->d_name, "FDR_", 4) == 0 &&
        strstr(entry->d_name, ".bin") != NULL) {

      char src_path[280];
      char dst_path[280];
      snprintf(src_path, sizeof(src_path), "%s/%s", SD_MOUNT_POINT,
               entry->d_name);
      snprintf(dst_path, sizeof(dst_path), "%s/%s", USB_MOUNT_POINT,
               entry->d_name);

      esp_err_t ret = copy_file(src_path, dst_path);
      if (ret == ESP_OK) {
        files_copied++;
      } else {
        errors++;
      }
    }
  }

  closedir(dir);

  if (files_copied == 0 && errors == 0) {
    ESP_LOGW(TAG, "No se encontraron archivos FDR_*.bin en la SD");
    return ESP_ERR_NOT_FOUND;
  }

  ESP_LOGI(TAG, "=== Transferencia completa: %d archivos copiados, %d errores ===",
           files_copied, errors);

  return (errors == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t usb_transfer_deinit(void) {
  if (!s_initialized) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Cerrando USB Host...");

  // Desmontar pendrive si está montado
  unmount_pendrive();

  // Desinstalar driver MSC
  msc_host_uninstall();

  // Detener tarea USB
  if (s_usb_task_handle != NULL) {
    vTaskDelete(s_usb_task_handle);
    s_usb_task_handle = NULL;
  }

  // Desinstalar stack USB Host
  esp_err_t ret = usb_host_uninstall();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Error al desinstalar USB Host: %s", esp_err_to_name(ret));
  }

  // Liberar cola
  if (s_app_queue != NULL) {
    vQueueDelete(s_app_queue);
    s_app_queue = NULL;
  }

  s_initialized = false;
  s_usb_ready = false;

  ESP_LOGI(TAG, "USB Host cerrado");
  return ESP_OK;
}

bool usb_transfer_is_ready(void) {
  // Si ya está listo, retornar inmediatamente
  if (s_usb_ready) {
    return true;
  }

  // Si está inicializado pero no listo, revisar si hay evento pendiente
  if (s_initialized && s_app_queue != NULL) {
    usb_app_msg_t msg;
    if (xQueueReceive(s_app_queue, &msg, 0) == pdTRUE) {
      if (msg.id == USB_EVT_CONNECTED) {
        mount_pendrive(msg.data.dev_address);
      }
    }
  }

  return s_usb_ready;
}
