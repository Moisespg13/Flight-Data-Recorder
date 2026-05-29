
/**
 * @file spi_link.c
 * @brief Enlace SPI inter-placa (lado Maestro).
 *
 * Espera la señal DRDY del Esclavo antes de leer. Cuando DRDY
 * sube a HIGH, realiza una transacción SPI y busca el Magic Byte
 * para extraer el paquete.
 */

#include "spi_link.h"
#include "crc_utils.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "protocol.h"
#include "spi_manager.h"
#include <string.h>

static const char *TAG = "SPI_LINK";

static QueueHandle_t s_fast_queue = NULL;
static QueueHandle_t s_slow_queue = NULL;

// Tamaño de un paquete completo (el más grande)
#define MAX_PKT_LEN (sizeof(fdr_header_t) + \
    ((sizeof(fast_data_payload_t) > sizeof(slow_data_payload_t)) \
         ? sizeof(fast_data_payload_t) \
         : sizeof(slow_data_payload_t)) + 4)

// Leemos 3x el tamaño del paquete para asegurar que al menos
// un paquete completo esté contenido en el buffer
#define SPI_RX_BUF_SIZE (MAX_PKT_LEN * 3)

DMA_ATTR static uint8_t rx_buffer[SPI_RX_BUF_SIZE];

static SemaphoreHandle_t s_drdy_sem = NULL;

static void IRAM_ATTR drdy_isr(void *arg) {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(s_drdy_sem, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void tsk_spi_link_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea SPI Link Maestro Iniciada");

  spi_device_handle_t spi_dev = spi_manager_get_master_handle();
  if (spi_dev == NULL) {
    ESP_LOGE(TAG, "Handle SPI Maestro no disponible!");
    vTaskDelete(NULL);
    return;
  }

  s_drdy_sem = xSemaphoreCreateBinary();

  // Configurar GPIO DRDY como entrada
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << PIN_SPI_DRDY),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE, // Pull-down: LOW cuando no hay señal
      .intr_type = GPIO_INTR_POSEDGE,
  };
  gpio_config(&io_conf);

  esp_err_t ret_isr = gpio_install_isr_service(0);
  if (ret_isr != ESP_OK && ret_isr != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(ret_isr));
  }
  gpio_isr_handler_add(PIN_SPI_DRDY, drdy_isr, NULL);

  ESP_LOGI(TAG, "MAX_PKT_LEN=%d, SPI_RX_BUF_SIZE=%d, DRDY=GPIO%d",
           (int)MAX_PKT_LEN, (int)SPI_RX_BUF_SIZE, PIN_SPI_DRDY);

  spi_transaction_t t;
  uint32_t ok_count = 0;
  uint32_t fail_count = 0;
  uint32_t drdy_hits = 0;

  while (1) {
    // Esperar a que el Esclavo aserte DRDY (timeout 500ms)
    if (xSemaphoreTake(s_drdy_sem, pdMS_TO_TICKS(500)) != pdTRUE) {
      // Timeout: leer de todas formas (fallback a polling ciego)
      // Esto permite funcionar aunque el cable DRDY no esté conectado
    }

    // DRDY HIGH o timeout → leer SPI
    drdy_hits++;

    memset(&t, 0, sizeof(t));
    t.length = SPI_RX_BUF_SIZE * 8;
    t.tx_buffer = NULL;
    t.rx_buffer = rx_buffer;

    esp_err_t ret = spi_device_transmit(spi_dev, &t);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error SPI: %s", esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // Escanear el buffer buscando el Magic Byte
    bool found = false;
    for (int i = 0; i <= (int)SPI_RX_BUF_SIZE - (int)sizeof(fdr_header_t); i++) {
      if (rx_buffer[i] != FDR_PROTO_MAGIC) continue;

      fdr_header_t *hdr = (fdr_header_t *)&rx_buffer[i];
      if (hdr->length > FDR_MAX_PAYLOAD) continue;

      size_t data_len = sizeof(fdr_header_t) + hdr->length;
      if (i + data_len + 4 > SPI_RX_BUF_SIZE) continue;

      uint32_t crc_rx = 0;
      memcpy(&crc_rx, &rx_buffer[i + data_len], 4);
      uint32_t crc_calc = fdr_calc_crc32(&rx_buffer[i], data_len);

      if (crc_rx != crc_calc) continue;

      // ¡Paquete válido!
      const uint8_t *payload_ptr = &rx_buffer[i + sizeof(fdr_header_t)];
      ok_count++;

      if (ok_count % 50 == 0) {
        ESP_LOGI(TAG, "PKT OK #%lu type=0x%02X seq=%u idx=%d (drdy=%lu)",
                 (unsigned long)ok_count, hdr->type, hdr->sequence, i,
                 (unsigned long)drdy_hits);
      }

      switch (hdr->type) {
      case PKT_TYPE_FAST_DATA:
        if (s_fast_queue && hdr->length == sizeof(fast_data_payload_t))
          xQueueSend(s_fast_queue, payload_ptr, 0);
        break;
      case PKT_TYPE_SLOW_DATA:
        if (s_slow_queue && hdr->length == sizeof(slow_data_payload_t))
          xQueueSend(s_slow_queue, payload_ptr, 0);
        break;
      }
      found = true;
      break;
    }

    if (!found) {
      fail_count++;
      if (fail_count % 500 == 0) {
        // Contar bytes no-cero en todo el buffer
        int nonzero = 0;
        for (int j = 0; j < (int)SPI_RX_BUF_SIZE; j++) {
          if (rx_buffer[j] != 0x00 && rx_buffer[j] != 0xFF) nonzero++;
        }
        ESP_LOGW(TAG, "FAIL #%lu (ok=%lu drdy=%lu) nonzero=%d/%d",
                 (unsigned long)fail_count, (unsigned long)ok_count,
                 (unsigned long)drdy_hits, nonzero, (int)SPI_RX_BUF_SIZE);
        // Dump primeros 58 bytes
        for (int row = 0; row < 58; row += 16) {
          int end = row + 16;
          if (end > 58) end = 58;
          char line[128];
          int pos = snprintf(line, sizeof(line), "  [%03d]: ", row);
          for (int j = row; j < end; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", rx_buffer[j]);
          }
          ESP_LOGI(TAG, "%s", line);
        }
      }
    }
  }
}

esp_err_t spi_link_init(QueueHandle_t q_rx_fast, QueueHandle_t q_rx_slow) {
  s_fast_queue = q_rx_fast;
  s_slow_queue = q_rx_slow;

  esp_err_t ret = spi_manager_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar SPI Manager");
    return ret;
  }

  ESP_LOGI(TAG, "SPI Link inicializado (fast_q=%s, slow_q=%s)",
           q_rx_fast ? "OK" : "NULL", q_rx_slow ? "OK" : "NULL");
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
