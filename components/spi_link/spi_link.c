
/**
 * @file spi_link.c
 * @brief Enlace SPI inter-placa (Maestro <-> Esclavo).
 *
 * Implementa las tareas de comunicación SPI para ambos roles:
 * - Esclavo: Consume datos de la cola, arma paquetes (Header + Payload + CRC)
 *   y los transmite al Maestro via spi_slave_transmit.
 * - Maestro: Sondea al Esclavo, valida Magic Byte y CRC,
 *   y deposita los datos válidos en una cola de recepción.
 */

#include "spi_link.h"
#include "crc_utils.h"         // IWYU pragma: keep
#include "esp_log.h"           // IWYU pragma: keep
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"    // IWYU pragma: keep
#include "freertos/task.h"     // IWYU pragma: keep
#include "pin_config.h"        // IWYU pragma: keep
#include "protocol.h"          // IWYU pragma: keep
#include "sdkconfig.h"         // IWYU pragma: keep
#include <string.h>            // IWYU pragma: keep

#ifdef CONFIG_FDR_ROLE_SLAVE
#include "driver/spi_slave.h" // IWYU pragma: keep
#elif defined(CONFIG_FDR_ROLE_MASTER)
#include "driver/spi_master.h" // IWYU pragma: keep
#include "spi_manager.h"       // IWYU pragma: keep
#endif

static const char *TAG = "SPI_LINK";

// Cola de datos a transmitir/recibir
static QueueHandle_t s_data_queue = NULL;

// ============================================================================
// ESCLAVO: Tarea de transmisión
// ============================================================================
#ifdef CONFIG_FDR_ROLE_SLAVE

// Buffer de transmisión SPI (DMA requiere alineación especial)
DMA_ATTR static uint8_t tx_buffer[sizeof(fdr_header_t) + FDR_MAX_PAYLOAD + 4];

static void tsk_spi_link_slave(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea SPI Link Esclavo Iniciada");

  fast_data_payload_t temp_payload;
  fdr_header_t *header = (fdr_header_t *)tx_buffer;
  uint32_t *crc_ptr;
  uint16_t seq = 0;

  spi_slave_transaction_t t;
  memset(&t, 0, sizeof(t));

  while (1) {
    // 1. Esperar datos de la cola de adquisición (Bloqueante)
    if (xQueueReceive(s_data_queue, &temp_payload, portMAX_DELAY) == pdTRUE) {

      // 2. Armar Cabecera
      header->magic = FDR_PROTO_MAGIC;
      header->type = PKT_TYPE_FAST_DATA;
      header->sequence = seq++;
      header->timestamp_ns = 0; // TODO: Sincronizar con timer local
      header->length = sizeof(fast_data_payload_t);

      // 3. Copiar Payload al buffer de TX (después del header)
      memcpy(tx_buffer + sizeof(fdr_header_t), &temp_payload,
             sizeof(fast_data_payload_t));

      // 4. Calcular CRC32 (Header + Payload)
      size_t total_len = sizeof(fdr_header_t) + sizeof(fast_data_payload_t);
      uint32_t crc = fdr_calc_crc32(tx_buffer, total_len);

      // 5. Agregar CRC al final
      crc_ptr = (uint32_t *)(tx_buffer + total_len);
      *crc_ptr = crc;
      total_len += 4; // 4 bytes del CRC

      // 6. Configurar Transacción SPI
      t.length = total_len * 8; // En bits
      t.tx_buffer = tx_buffer;
      t.rx_buffer = NULL;

      // 7. Enviar (Bloquea hasta que el Maestro inicie la transacción)
      esp_err_t ret = spi_slave_transmit(PIN_LINK_SPI_HOST, &t, portMAX_DELAY);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error SPI TX: %s", esp_err_to_name(ret));
      }
    }
  }
}

// ============================================================================
// MAESTRO: Tarea de recepción
// ============================================================================
#elif defined(CONFIG_FDR_ROLE_MASTER)

// Buffer de recepción SPI (DMA requiere alineación especial)
DMA_ATTR static uint8_t rx_buffer[sizeof(fdr_header_t) + FDR_MAX_PAYLOAD + 4];

static void tsk_spi_link_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea SPI Link Maestro Iniciada");

  spi_device_handle_t spi_dev = spi_manager_get_master_handle();
  if (spi_dev == NULL) {
    ESP_LOGE(TAG, "Handle SPI Maestro no disponible!");
    vTaskDelete(NULL);
    return;
  }

  spi_transaction_t t;
  // Tamaño esperado de un paquete FAST_DATA completo
  const size_t expected_len =
      sizeof(fdr_header_t) + sizeof(fast_data_payload_t) + 4;

  while (1) {
    // 1. Preparar transacción de lectura
    memset(&t, 0, sizeof(t));
    t.length = expected_len * 8; // En bits
    t.tx_buffer = NULL;          // No enviamos nada al Esclavo (por ahora)
    t.rx_buffer = rx_buffer;

    // 2. Ejecutar transacción (Maestro genera CLK + CS)
    esp_err_t ret = spi_device_transmit(spi_dev, &t);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error SPI RX: %s", esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(100)); // Reintentar después de un rato
      continue;
    }

    // 3. Validar paquete recibido
    fdr_header_t *header = (fdr_header_t *)rx_buffer;

    // 3a. Verificar Magic Byte
    if (header->magic != FDR_PROTO_MAGIC) {
      ESP_LOGW(TAG, "Magic inválido: 0x%02X (esperado 0x%02X)", header->magic,
               FDR_PROTO_MAGIC);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // 3b. Verificar CRC32
    size_t data_len = sizeof(fdr_header_t) + header->length;
    uint32_t crc_recibido = *(uint32_t *)(rx_buffer + data_len);
    uint32_t crc_calculado = fdr_calc_crc32(rx_buffer, data_len);

    if (crc_recibido != crc_calculado) {
      ESP_LOGW(TAG, "CRC inválido! Recibido=0x%08lX Calculado=0x%08lX",
               (unsigned long)crc_recibido, (unsigned long)crc_calculado);
      continue;
    }

    // 4. Paquete válido! Procesar según tipo
    ESP_LOGD(TAG, "PKT OK: type=%d seq=%u len=%u", header->type,
             header->sequence, header->length);

    if (header->type == PKT_TYPE_FAST_DATA && s_data_queue != NULL) {
      // Extraer payload y depositar en la cola del Maestro
      fast_data_payload_t *payload =
          (fast_data_payload_t *)(rx_buffer + sizeof(fdr_header_t));

      if (xQueueSend(s_data_queue, payload, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Cola RX Maestro LLENA (Drop)");
      }
    }

    // Polling rate: ~100Hz (ajustable según necesidad)
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

#endif // CONFIG_FDR_ROLE_MASTER

// ============================================================================
// API Pública
// ============================================================================

esp_err_t spi_link_init(QueueHandle_t q_tx_data) {
  s_data_queue = q_tx_data;
  ESP_LOGI(TAG, "SPI Link inicializado (cola=%s)", q_tx_data ? "OK" : "NULL");
  return ESP_OK;
}

esp_err_t spi_link_start(void) {
#ifdef CONFIG_FDR_ROLE_SLAVE
  // Esclavo: Tarea de transmisión, Prio 6
  BaseType_t ok = xTaskCreate(tsk_spi_link_slave, "tsk_spi_link_slave", 4096,
                              NULL, 6, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea SPI Link Esclavo");
    return ESP_ERR_NO_MEM;
  }

#elif defined(CONFIG_FDR_ROLE_MASTER)
  // Maestro: Tarea de recepción, Prio 6
  BaseType_t ok = xTaskCreate(tsk_spi_link_master, "tsk_spi_link_master", 4096,
                              NULL, 6, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea SPI Link Maestro");
    return ESP_ERR_NO_MEM;
  }

#else
  ESP_LOGE(TAG, "Rol no definido, no se puede iniciar SPI Link");
  return ESP_ERR_INVALID_STATE;
#endif

  return ESP_OK;
}
