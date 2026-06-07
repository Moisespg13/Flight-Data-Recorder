
/**
 * @file spi_link.c
 * @brief Enlace SPI inter-placa (lado Maestro) — MODO DRDY HANDSHAKE.
 *
 * Protocolo de 3 pasos con el Esclavo (RP2040):
 *
 *   1. Master espera DRDY HIGH (GPIO45) → Slave tiene datos listos
 *   2. Master hace spi_device_transmit (174 bytes @ 8MHz ≈ 175µs)
 *   3. Master espera DRDY LOW → Slave confirmó fin de transacción
 *   4. Repetir
 *
 * GPIO45 es STRAP pin (VDD_SPI voltage select). Es seguro como input
 * porque el pull-down interno del ESP32-S3 lo mantiene LOW al boot,
 * y el RP2040 no aserta DRDY hasta recibir SYNC_TRIG post-boot.
 */

#include "spi_link.h"
#include "crc_utils.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pin_config.h"
#include "protocol.h"
#include "spi_manager.h"
#include "supervisor.h"
#include <string.h>

static const char *TAG = "SPI_LINK";

static QueueHandle_t s_fast_queue = NULL;
static QueueHandle_t s_slow_queue = NULL;

// Tamaño de un paquete completo (el más grande)
#define MAX_PKT_LEN (sizeof(fdr_header_t) + \
    ((sizeof(fast_data_payload_t) > sizeof(slow_data_payload_t)) \
         ? sizeof(fast_data_payload_t) \
         : sizeof(slow_data_payload_t)) + 4)

// Buffer SPI: ahora se lee exactamente un paquete (58 bytes) gracias a la sincronización CS/DRDY
#define SPI_RX_BUF_SIZE (MAX_PKT_LEN)

DMA_ATTR static uint8_t rx_buffer[SPI_RX_BUF_SIZE];

// Contadores de throughput
static uint32_t s_tp_fast_count = 0;
static int64_t  s_tp_last_us = 0;

// Timeout para DRDY: si no sube/baja en este tiempo, logear warning
#define DRDY_TIMEOUT_US 50000  // 50ms

static void tsk_spi_link_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea SPI Link Maestro Iniciada (DRDY handshake, GPIO%d)",
           PIN_SPI_DRDY);

  spi_device_handle_t spi_dev = spi_manager_get_master_handle();
  if (spi_dev == NULL) {
    ESP_LOGE(TAG, "Handle SPI Maestro no disponible!");
    vTaskDelete(NULL);
    return;
  }

  // Configurar como input para DRDY
  gpio_config_t drdy_cfg = {
      .pin_bit_mask = (1ULL << PIN_SPI_DRDY),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,  // Pull-down: DRDY idle = LOW
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&drdy_cfg);

  ESP_LOGI(TAG, "MAX_PKT_LEN=%d, SPI_RX_BUF_SIZE=%d, DRDY=GPIO%d",
           (int)MAX_PKT_LEN, (int)SPI_RX_BUF_SIZE, PIN_SPI_DRDY);

  // Verificar estado inicial de DRDY
  int drdy_init = gpio_get_level(PIN_SPI_DRDY);
  ESP_LOGI(TAG, "DRDY estado inicial: %s",
           drdy_init ? "HIGH (⚠️ Slave ya tiene datos?)" : "LOW (OK, esperado)");

  spi_transaction_t t;
  uint32_t ok_count = 0;
  uint32_t fail_count = 0;
  uint32_t total_tx = 0;
  uint32_t drdy_timeout_count = 0;

  // Registrar en el supervisor (timeout 5 segundos)
  supervisor_register(TASK_ID_SPI_LINK, "spi_link", 5000);

  // Esperar a que el Slave arranque
  ESP_LOGI(TAG, "Esperando 500ms para que el Slave arranque...");
  vTaskDelay(pdMS_TO_TICKS(500));

  while (1) {
    // ================================================================
    // PASO 1: Esperar DRDY HIGH (Slave tiene datos listos)
    // ================================================================
    int64_t wait_start = esp_timer_get_time();
    while (1) {
      if (gpio_get_level(PIN_SPI_DRDY) == 1) {
        esp_rom_delay_us(2); // Debounce de crosstalk
        if (gpio_get_level(PIN_SPI_DRDY) == 1) break; // DRDY real
      }

      int64_t elapsed = esp_timer_get_time() - wait_start;
      if (elapsed > DRDY_TIMEOUT_US) {
        drdy_timeout_count++;
        if (drdy_timeout_count <= 10 || drdy_timeout_count % 1000 == 0) {
          ESP_LOGW(TAG, "DRDY no subió en %dms (timeout #%lu)",
                   (int)(DRDY_TIMEOUT_US / 1000),
                   (unsigned long)drdy_timeout_count);
        }
        wait_start = esp_timer_get_time(); // Reset para no spamear
      }
    }

    // ================================================================
    // PASO 2: Transacción SPI (174 bytes @ 8MHz ≈ 175µs)
    // ================================================================
    total_tx++;

    memset(&t, 0, sizeof(t));
    t.length = SPI_RX_BUF_SIZE * 8;
    t.tx_buffer = NULL;
    t.rx_buffer = rx_buffer;

    esp_err_t ret = spi_device_transmit(spi_dev, &t);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error SPI: %s", esp_err_to_name(ret));
      continue;
    }

    // ================================================================
    // PASO 3: Micro-gap de seguridad (2µs)
    // ================================================================
    // spi_device_transmit() ya tiene ~20µs de overhead al retornar,
    // tiempo más que suficiente para que el Slave detecte CS HIGH y
    // baje DRDY. Agregamos 2µs de hardware delay solo por paranoia.
    // No esperamos activamente por DRDY LOW porque si el Slave está
    // atrasado, bajará y subirá DRDY tan rápido que el Master se lo
    // perdería y quedaría colgado en un timeout.
    esp_rom_delay_us(2);

    // ================================================================
    // PASO 4: Buscar paquete válido (Magic + CRC)
    // ================================================================
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
      supervisor_checkin(TASK_ID_SPI_LINK);

      if (ok_count % 3000 == 0) {
        ESP_LOGI(TAG, "PKT OK #%lu type=0x%02X seq=%u (tx=%lu, hit=%.0f%%)",
                 (unsigned long)ok_count, hdr->type, hdr->sequence,
                 (unsigned long)total_tx,
                 (float)ok_count * 100.0f / (float)total_tx);
      }

      switch (hdr->type) {
      case PKT_TYPE_FAST_DATA:
        if (s_fast_queue && hdr->length == sizeof(fast_data_payload_t)) {
          xQueueSend(s_fast_queue, payload_ptr, 0);
          s_tp_fast_count++;
        }
        break;
      case PKT_TYPE_SLOW_DATA:
        if (s_slow_queue && hdr->length == sizeof(slow_data_payload_t))
          xQueueSend(s_slow_queue, payload_ptr, 0);
        break;
      }

      found = true;
      i += data_len + 4 - 1;
    }

    if (!found) {
      fail_count++;
    }

    // ================================================================
    // PASO 5: Esperar DRDY LOW (Esclavo confirmó fin)
    // ================================================================
    while (gpio_get_level(PIN_SPI_DRDY) == 1) {
      esp_rom_delay_us(1);
    }

    // Log de throughput cada 5 segundos
    int64_t now_us = esp_timer_get_time();
    if (s_tp_last_us == 0) s_tp_last_us = now_us;
    if (now_us - s_tp_last_us >= 5000000) {
      float rate = (float)s_tp_fast_count /
                   ((float)(now_us - s_tp_last_us) / 1000000.0f);
      ESP_LOGI(TAG, "THROUGHPUT: %.0f fast pkts/sec (ok=%lu fail=%lu tx=%lu drdy_to=%lu)",
               rate, (unsigned long)ok_count, (unsigned long)fail_count,
               (unsigned long)total_tx, (unsigned long)drdy_timeout_count);
      s_tp_fast_count = 0;
      s_tp_last_us = now_us;
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
  BaseType_t ok = xTaskCreatePinnedToCore(
      tsk_spi_link_master, "tsk_spi_link",
      4096, NULL,
      configMAX_PRIORITIES - 1,  // Prioridad máxima
      NULL,
      1);  // Core 1 dedicado
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea SPI Link Maestro");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
