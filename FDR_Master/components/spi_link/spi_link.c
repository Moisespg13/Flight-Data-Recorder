
/**
 * @file spi_link.c
 * @brief Enlace SPI inter-placa (lado Maestro).
 *
 * Tarea de recepción: Sondea al Esclavo (RP2040), valida Magic Byte
 * y CRC, y deposita los datos válidos en una cola de recepción.
 */

#include "spi_link.h"
#include "crc_utils.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "protocol.h"
#include "spi_manager.h"
#include <string.h>

static const char *TAG = "SPI_LINK";

// Cola de datos recibidos
static QueueHandle_t s_data_queue = NULL;

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
  const size_t expected_len =
      sizeof(fdr_header_t) + sizeof(fast_data_payload_t) + 4;

  while (1) {
    memset(&t, 0, sizeof(t));
    t.length = expected_len * 8;
    t.tx_buffer = NULL;
    t.rx_buffer = rx_buffer;

    esp_err_t ret = spi_device_transmit(spi_dev, &t);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error SPI RX: %s", esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    fdr_header_t *header = (fdr_header_t *)rx_buffer;

    // Verificar Magic Byte
    if (header->magic != FDR_PROTO_MAGIC) {
      ESP_LOGW(TAG, "Magic inválido: 0x%02X (esperado 0x%02X)", header->magic,
               FDR_PROTO_MAGIC);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    // Verificar CRC32
    size_t data_len = sizeof(fdr_header_t) + header->length;
    uint32_t crc_recibido = *(uint32_t *)(rx_buffer + data_len);
    uint32_t crc_calculado = fdr_calc_crc32(rx_buffer, data_len);

    if (crc_recibido != crc_calculado) {
      ESP_LOGW(TAG, "CRC inválido! Recibido=0x%08lX Calculado=0x%08lX",
               (unsigned long)crc_recibido, (unsigned long)crc_calculado);
      continue;
    }

    // Paquete válido
    ESP_LOGD(TAG, "PKT OK: type=%d seq=%u len=%u", header->type,
             header->sequence, header->length);

    if (header->type == PKT_TYPE_FAST_DATA && s_data_queue != NULL) {
      fast_data_payload_t *payload =
          (fast_data_payload_t *)(rx_buffer + sizeof(fdr_header_t));

      if (xQueueSend(s_data_queue, payload, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Cola RX Maestro LLENA (Drop)");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz polling
  }
}

esp_err_t spi_link_init(QueueHandle_t q_rx_data) {
  s_data_queue = q_rx_data;

  // Inicializar el bus SPI
  esp_err_t ret = spi_manager_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar SPI Manager");
    return ret;
  }

  ESP_LOGI(TAG, "SPI Link inicializado (cola=%s)", q_rx_data ? "OK" : "NULL");
  return ESP_OK;
}

esp_err_t spi_link_start(void) {
  BaseType_t ok = xTaskCreate(tsk_spi_link_master, "tsk_spi_link_master", 4096,
                              NULL, 6, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea SPI Link Maestro");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
