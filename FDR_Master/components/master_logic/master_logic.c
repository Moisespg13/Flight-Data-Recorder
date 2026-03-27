
/**
 * @file master_logic.c
 * @brief Lógica principal del Maestro (ESP32-S3).
 *
 * Inicializa el enlace SPI para recibir datos del Esclavo (RP2040),
 * valida los paquetes (Magic + CRC), lee señales discretas directas,
 * controla la señal SYNC_TRIG para sincronizar al Esclavo, y lanza
 * las tareas de procesamiento y almacenamiento.
 */

#include "master_logic.h"
#include "discrete_signals.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "protocol.h"
#include "spi_link.h"

static const char *TAG = "MASTER_LOGIC";

// Cola donde spi_link deposita los paquetes recibidos del Esclavo
static QueueHandle_t q_rx_data = NULL;

// ============================================================================
// Tarea: Procesamiento de datos analógicos recibidos del Esclavo
// ============================================================================
static void tsk_data_proc_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Procesamiento de Datos Iniciada");

  fast_data_payload_t payload;

  while (1) {
    if (xQueueReceive(q_rx_data, &payload, portMAX_DELAY) == pdTRUE) {

      ESP_LOGI(TAG, "RX Ch[0..3]: %d | %d | %d | %d", payload.channels[0],
               payload.channels[1], payload.channels[2], payload.channels[3]);

      // TODO: Almacenamiento:
      //   - Escribir a MicroSD
      //   - Escribir a eMMC (redundancia)
      //   - Agregar timestamp GNSS
    }
  }
}

// ============================================================================
// Tarea: Lectura periódica de señales discretas
// ============================================================================
static void tsk_discrete_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Lectura de Discretas Iniciada");

  discrete_payload_t disc_payload;
  uint16_t prev_states = 0;

  while (1) {
    uint16_t current_states;
    if (discrete_signals_read(&current_states) == ESP_OK) {
      // Solo loguear si hubo cambio (evitar spam)
      if (current_states != prev_states) {
        disc_payload.timestamp_ns = (uint64_t)esp_timer_get_time() * 1000;
        disc_payload.states = current_states;

        ESP_LOGI(TAG, "Discretas: 0x%04X", current_states);

        // TODO: Enviar disc_payload a la cola de almacenamiento
        prev_states = current_states;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz
  }
}

// ============================================================================
// Configuración de la señal SYNC_TRIG
// ============================================================================
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

// ============================================================================
// API Pública
// ============================================================================
esp_err_t master_logic_start(void) {
  ESP_LOGI(TAG, "Arrancando Lógica de Maestro...");

  // 0. Configurar señal de sincronización
  esp_err_t ret = sync_trigger_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar SYNC_TRIG");
    return ret;
  }
  ESP_LOGI(TAG, "SYNC_TRIG (GPIO %d) configurado como salida.", PIN_SYNC_TRIG);

  // 1. Inicializar lecturas discretas
  ret = discrete_signals_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar entradas discretas");
    return ret;
  }

  // 2. Crear Cola de recepción (datos del Esclavo)
  q_rx_data = xQueueCreate(50, sizeof(fast_data_payload_t));
  if (q_rx_data == NULL) {
    ESP_LOGE(TAG, "Error al crear cola rx_data");
    return ESP_ERR_NO_MEM;
  }

  // 3. Inicializar Enlace SPI
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

  // 4. Crear Tarea de Procesamiento de Analógicas (Prio 5)
  BaseType_t ok =
      xTaskCreate(tsk_data_proc_master, "data_proc", 4096, NULL, 5, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de procesamiento");
    return ESP_ERR_NO_MEM;
  }

  // 5. Crear Tarea de Lectura de Discretas (Prio 5)
  ok = xTaskCreate(tsk_discrete_master, "discrete", 2048, NULL, 5, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de discretas");
    return ESP_ERR_NO_MEM;
  }

  // 6. Disparar sincronización: Esclavo puede arrancar
  ESP_LOGI(TAG, "Maestro listo. Enviando SYNC_TRIG al Esclavo (RP2040)...");
  gpio_set_level(PIN_SYNC_TRIG, 1);

  return ESP_OK;
}
