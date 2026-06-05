/**
 * @file FDR_Master.c
 * @brief Punto de entrada principal del firmware Maestro (ESP32-S3).
 *
 * Determina el modo de operación del MAD automáticamente:
 *
 *   MODO DESCARGA (si hay pendrive conectado al arranque):
 *     No arranca las tareas de adquisición. Monta la SD y el USB
 *     Host para permitir la copia de archivos al pendrive.
 *
 *   MODO VUELO (si no hay pendrive):
 *     Inicializa SPI, sensores, GPS, almacenamiento en SD y todas
 *     las tareas de adquisición de datos en tiempo real.
 *
 * La detección es automática: al encender, el sistema intenta
 * inicializar el USB Host y espera 3 segundos. Si detecta un
 * pendrive → modo descarga. Si no → modo vuelo.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "led_indicator.h"
#include "master_logic.h"
#include "pin_config.h"
#include "protocol.h"
#include "sd_storage.h"
#include "usb_transfer.h"

static const char *TAG = "FDR_MASTER";

/**
 * @brief Detecta si hay un pendrive conectado al arranque.
 *
 * Inicializa el stack USB Host y espera hasta 3 segundos
 * para la enumeración de un dispositivo MSC. Si se detecta,
 * retorna MAD_MODE_DOWNLOAD. Si no, cierra el USB Host y
 * retorna MAD_MODE_FLIGHT.
 *
 * @return MAD_MODE_DOWNLOAD o MAD_MODE_FLIGHT
 */
static mad_mode_t detect_boot_mode(void) {
  ESP_LOGI(TAG, "Detectando modo de arranque...");
  ESP_LOGI(TAG, "Si hay pendrive conectado -> MODO DESCARGA");
  ESP_LOGI(TAG, "Si no hay pendrive        -> MODO VUELO");

  // Intentar inicializar USB Host
  esp_err_t ret = usb_transfer_init();
  if (ret != ESP_OK) {
    // Si USB Host no se pudo inicializar, ir directo a vuelo
    ESP_LOGW(TAG, "USB Host no disponible. Arrancando en MODO VUELO.");
    return MAD_MODE_FLIGHT;
  }

  // Esperar hasta 3 segundos a que se conecte un pendrive
  ESP_LOGI(TAG, "USB Host activo. Esperando pendrive (3 seg)...");
  for (int i = 0; i < 30; i++) {
    if (usb_transfer_is_ready()) {
      ESP_LOGI(TAG, "Pendrive detectado!");
      return MAD_MODE_DOWNLOAD;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // No se detectó pendrive: cerrar USB y arrancar en modo vuelo
  ESP_LOGI(TAG, "No se detectó pendrive. Cerrando USB Host.");
  usb_transfer_deinit();
  return MAD_MODE_FLIGHT;
}

/**
 * @brief Modo Descarga: copia archivos de SD a pendrive.
 */
static void run_download_mode(void) {
  ESP_LOGI(TAG, "************************************");
  ESP_LOGI(TAG, "***   MODO DESCARGA ACTIVADO     ***");
  ESP_LOGI(TAG, "************************************");

  // 1. Montar la MicroSD
  esp_err_t ret = sd_storage_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "ERROR: No se puede montar la MicroSD");
    return;
  }
  ESP_LOGI(TAG, "MicroSD montada correctamente");

  // 2. Si el pendrive no está listo aún (por alguna razón), esperarlo
  if (!usb_transfer_is_ready()) {
    ESP_LOGI(TAG, "Esperando que el pendrive termine de montar...");
    for (int i = 0; i < 50; i++) {
      if (usb_transfer_is_ready()) break;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  if (!usb_transfer_is_ready()) {
    ESP_LOGE(TAG, "ERROR: Pendrive no disponible después de esperar");
    usb_transfer_deinit();
    led_set_mode(LED_MODE_DOWNLOAD_ERROR);
    return;
  }

  // 3. Copiar archivos
  ESP_LOGI(TAG, "Iniciando copia de archivos a pendrive...");
  led_set_mode(LED_MODE_DOWNLOAD_PROGRESS);
  ret = usb_transfer_copy_all();
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "************************************");
    ESP_LOGI(TAG, "***   DESCARGA COMPLETADA OK     ***");
    ESP_LOGI(TAG, "************************************");
    led_set_mode(LED_MODE_DOWNLOAD_DONE);
  } else {
    ESP_LOGE(TAG, "*** DESCARGA CON ERRORES ***");
    led_set_mode(LED_MODE_DOWNLOAD_ERROR);
  }

  // 4. Limpiar
  usb_transfer_deinit();

  ESP_LOGI(TAG, "Puede retirar el pendrive y apagar el equipo.");

  // Quedarse en idle (no reiniciar, no volar)
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

/**
 * @brief Modo Vuelo: arranca toda la adquisición de datos.
 */
static void run_flight_mode(void) {
  ESP_LOGI(TAG, "************************************");
  ESP_LOGI(TAG, "***     MODO VUELO ACTIVADO      ***");
  ESP_LOGI(TAG, "************************************");
  ESP_LOGI(TAG, "Pines SPI Link: MISO=%d, MOSI=%d, CLK=%d, CS=%d",
           PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CLK, PIN_SPI_CS);
  ESP_LOGI(TAG, "Protocol Magic: 0x%02X", FDR_PROTO_MAGIC);

  led_set_mode(LED_MODE_FLIGHT_OK);

  esp_err_t ret = master_logic_start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error fatal al iniciar lógica del Maestro!");
    led_set_mode(LED_MODE_FLIGHT_ERROR);
  }
}

void app_main(void) {
  // Inicializar NVS (necesario para ambos modos)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "=== FDR MAESTRO (ESP32-S3) ===");

  // Inicializar LED de estado
  led_init();

  // Detectar modo automáticamente (pendrive = descarga, sin pendrive = vuelo)
  mad_mode_t mode = detect_boot_mode();

  if (mode == MAD_MODE_DOWNLOAD) {
    run_download_mode();
  } else {
    run_flight_mode();
  }

  // Las tareas de FreeRTOS hacen todo el trabajo
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
