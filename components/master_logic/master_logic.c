
/**
 * @file master_logic.c
 * @brief Lógica principal del Maestro (Placa de Almacenamiento y Gestión).
 *
 * Inicializa el enlace SPI para recibir datos del Esclavo, valida
 * los paquetes (Magic + CRC) y lanza la tarea de procesamiento
 * que eventualmente almacenará los datos en MicroSD/eMMC.
 * También controla la señal SYNC_TRIG para sincronizar al Esclavo.
 */

#include "master_logic.h"
#include "driver/gpio.h"       // IWYU pragma: keep
#include "esp_log.h"           // IWYU pragma: keep
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"    // IWYU pragma: keep
#include "freertos/task.h"     // IWYU pragma: keep
#include "pin_config.h"        // IWYU pragma: keep
#include "protocol.h"          // IWYU pragma: keep
#include "sdkconfig.h"         // IWYU pragma: keep
#include "spi_link.h"          // IWYU pragma: keep

#ifdef CONFIG_FDR_ROLE_MASTER

static const char *TAG = "MASTER_LOGIC";

// Cola donde spi_link deposita los paquetes recibidos del Esclavo
static QueueHandle_t q_rx_data = NULL;

// Tarea de procesamiento de datos recibidos
static void tsk_data_proc(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Procesamiento de Datos Iniciada");

  fast_data_payload_t payload;

  while (1) {
    // Esperar dato válido del Esclavo (ya pasó validación Magic + CRC en
    // spi_link)
    if (xQueueReceive(q_rx_data, &payload, portMAX_DELAY) == pdTRUE) {

      // Por ahora: Loguear los primeros 4 canales como verificación
      ESP_LOGI(TAG, "RX Ch[0..3]: %d | %d | %d | %d", payload.channels[0],
               payload.channels[1], payload.channels[2], payload.channels[3]);

      // TODO: Aquí irá la lógica de almacenamiento:
      //   - Escribir a MicroSD
      //   - Escribir a eMMC (redundancia)
      //   - Agregar timestamp GNSS
    }
  }
}

// Configura PIN_SYNC_TRIG como salida (inicialmente LOW)
static esp_err_t sync_trigger_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << PIN_SYNC_TRIG),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    return ret;
  }
  // Inicialmente LOW: Esclavo NO debe arrancar todavía
  gpio_set_level(PIN_SYNC_TRIG, 0);
  return ESP_OK;
}

esp_err_t master_logic_start(void) {
  ESP_LOGI(TAG, "Arrancando Lógica de Maestro...");

  // 0. Configurar señal de sincronización
  // ---------------------------------------------------------
  esp_err_t ret = sync_trigger_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar SYNC_TRIG");
    return ret;
  }
  ESP_LOGI(TAG, "SYNC_TRIG (GPIO %d) configurado como salida.", PIN_SYNC_TRIG);

  // 1. Crear Cola de recepción
  // ---------------------------------------------------------
  q_rx_data = xQueueCreate(50, sizeof(fast_data_payload_t));
  if (q_rx_data == NULL) {
    ESP_LOGE(TAG, "Error al crear cola rx_data");
    return ESP_ERR_NO_MEM;
  }

  // 2. Inicializar Enlace SPI
  // ---------------------------------------------------------
  ret = spi_link_init(q_rx_data);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar SPI Link");
    return ret;
  }

  ret = spi_link_start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al arrancar SPI Link");
    return ret;
  }

  // 3. Crear Tarea de Procesamiento (Prio 5 = Media)
  // ---------------------------------------------------------
  BaseType_t ok =
      xTaskCreate(tsk_data_proc, "tsk_data_proc", 4096, NULL, 5, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de procesamiento");
    return ESP_ERR_NO_MEM;
  }

  // 4. Disparar sincronización: Esclavo puede arrancar
  // ---------------------------------------------------------
  ESP_LOGI(TAG, "Maestro listo. Enviando SYNC_TRIG al Esclavo...");
  gpio_set_level(PIN_SYNC_TRIG, 1);

  return ESP_OK;
}

#else // CONFIG_FDR_ROLE_SLAVE

esp_err_t master_logic_start(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif
