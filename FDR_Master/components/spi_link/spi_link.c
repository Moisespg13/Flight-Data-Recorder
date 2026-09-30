
/**
 * @file spi_link.c
 * @brief Enlace SPI inter-placa (lado Maestro) — MODO DRDY HANDSHAKE.
 *
 * Protocolo de 3 pasos con el Esclavo (RP2040):
 *
 *   1. Master espera DRDY HIGH → Slave tiene datos listos
 *   2. Master hace spi_device_transmit de un paquete completo (58 bytes)
 *   3. Master espera DRDY LOW → Slave confirmó fin de transacción
 *   4. Repetir
 *
 * El pin de DRDY sale de pin_config.h y el reloj del bus de spi_manager.c;
 * este archivo es idéntico en el árbol de producción y en el testbench.
 *
 * La espera del paso 1 es por interrupción (flanco ascendente de DRDY) y
 * no por sondeo: esta tarea corre fijada al core 1 y un bucle de sondeo
 * dejaba sin CPU a IDLE1 (disparando el Task WDT) y a cualquier otra
 * tarea fijada a ese core.
 */

#include "spi_link.h"
#include "crc_utils.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
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

static TaskHandle_t s_link_task = NULL;

// Tamaño de un paquete completo (el más grande: lote rápido de 4 o un lento)
#define MAX_PKT_LEN (sizeof(fdr_header_t) + \
    ((sizeof(fast_data_spi_payload_t) > sizeof(slow_data_payload_t)) \
         ? sizeof(fast_data_spi_payload_t) \
         : sizeof(slow_data_payload_t)) + 4)

// Buffer SPI: un paquete lento son 58 bytes; uno rápido, 178.
#define SPI_RX_BUF_SIZE (MAX_PKT_LEN)

DMA_ATTR static uint8_t rx_buffer[SPI_RX_BUF_SIZE];

// Si DRDY no sube en este tiempo, el esclavo puede haberse reiniciado y
// estar esperando un flanco de bajada de CS: le mandamos una trama vacía.
#define DRDY_TIMEOUT_MS 50

// ============================================================================
// Métricas del enlace
// ============================================================================
// Estos contadores son la evidencia de integridad del enlace: sin ellos no
// hay forma de cuantificar cuántas muestras se perdieron durante un vuelo.
typedef struct {
  uint32_t tx_total;      // Transacciones SPI iniciadas
  uint32_t pkt_ok;        // Paquetes con magic + CRC válidos
  uint32_t pkt_bad;       // Transacciones sin ningún paquete válido
  uint32_t drdy_timeouts; // Vencimientos esperando DRDY
  uint32_t drdy_stale;    // Flancos cuyo dato ya no estaba en oferta
  uint32_t dup_fast;      // Paquetes rápidos repetidos (misma secuencia)
  uint32_t dup_slow;      // Paquetes lentos repetidos (misma secuencia)
  uint32_t lost_fast;     // Paquetes rápidos perdidos (salto de secuencia)
  uint32_t lost_slow;     // Paquetes lentos perdidos (salto de secuencia)
  uint32_t drop_fast;     // Paquetes rápidos descartados por cola llena
  uint32_t drop_slow;     // Paquetes lentos descartados por cola llena
} link_stats_t;

static link_stats_t s_stats = {0};

// Seguimiento de secuencia por tipo de paquete
static uint16_t s_last_seq_fast = 0;
static uint16_t s_last_seq_slow = 0;
static bool s_seq_fast_init = false;
static bool s_seq_slow_init = false;

// Throughput
static uint32_t s_tp_fast_count = 0;
static int64_t s_tp_last_us = 0;

/**
 * @brief Cuenta los paquetes faltantes entre dos números de secuencia.
 *
 * La resta en uint16_t maneja el wrap de 65535 a 0 sin caso especial.
 */
static uint32_t seq_gap(uint16_t last, uint16_t current) {
  return (uint32_t)(uint16_t)(current - last - 1);
}

// ============================================================================
// ISR de DRDY
// ============================================================================
static void IRAM_ATTR drdy_isr_handler(void *arg) {
  (void)arg;
  BaseType_t higher_woken = pdFALSE;
  if (s_link_task != NULL) {
    vTaskNotifyGiveFromISR(s_link_task, &higher_woken);
  }
  if (higher_woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}

/**
 * @brief Transacción vacía para forzar un flanco de CS.
 *
 * Si el esclavo arrancó tarde o se reinició, está esperando un flanco de
 * bajada de CS para sincronizarse. El gating por CS del PIO garantiza que
 * estos pulsos no le roben bytes al stream del próximo paquete.
 */
static void poke_slave(spi_device_handle_t spi_dev) {
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8;
  spi_device_transmit(spi_dev, &t);
}

/**
 * @brief Espera a que el esclavo ofrezca un paquete, durmiendo hasta la ISR.
 *
 * Sincroniza por FLANCO de DRDY, no por nivel. El esclavo baja DRDY en su ISR
 * de CS y la vuelve a subir en cuanto arma el paquete siguiente, así que el
 * pulso bajo puede ser más corto que el procesamiento del maestro y no se
 * puede usar el nivel para delimitar transacciones: esperar a ver DRDY en LOW
 * lleva a que el maestro espere una bajada que ya pasó mientras el esclavo
 * espera un CS que no llega.
 *
 * Cada flanco ascendente incrementa la notificación. Se consume de a uno
 * (xClearCountOnExit = pdFALSE) para no perder flancos que hayan llegado
 * mientras procesábamos el paquete anterior.
 *
 * @return true si hay un paquete en oferta, false si hay que reintentar.
 */
static bool wait_drdy_edge(spi_device_handle_t spi_dev) {
  if (ulTaskNotifyTake(pdFALSE, pdMS_TO_TICKS(DRDY_TIMEOUT_MS)) == 0) {
    s_stats.drdy_timeouts++;
    if (s_stats.drdy_timeouts <= 10 || s_stats.drdy_timeouts % 1000 == 0) {
      ESP_LOGW(TAG, "Sin flanco de DRDY en %d ms (timeout #%lu)",
               DRDY_TIMEOUT_MS, (unsigned long)s_stats.drdy_timeouts);
    }
    poke_slave(spi_dev);
    return false;
  }

  // El flanco dice que hubo un paquete; el nivel dice si sigue en oferta. Si
  // el esclavo ya lo retiró (por su propio timeout de 10 ms), no tiene sentido
  // clockear: lo contamos y esperamos el flanco siguiente.
  if (gpio_get_level(PIN_SPI_DRDY) == 0) {
    s_stats.drdy_stale++;
    return false;
  }

  return true;
}

/**
 * @brief Encola un payload y contabiliza descartes y saltos de secuencia.
 */
static void dispatch_packet(const fdr_header_t *hdr, const uint8_t *payload) {
  switch (hdr->type) {
  case PKT_TYPE_FAST_DATA:
    if (s_fast_queue == NULL || hdr->length != sizeof(fast_data_spi_payload_t)) {
      break;
    }
    if (s_seq_fast_init) {
      if (hdr->sequence == s_last_seq_fast) {
        s_stats.dup_fast++;
        break;
      }
      s_stats.lost_fast +=
          seq_gap(s_last_seq_fast, hdr->sequence) * FAST_SAMPLES_PER_SPI_PKT;
    }
    s_last_seq_fast = hdr->sequence;
    s_seq_fast_init = true;

    {
      const fast_data_spi_payload_t *batch =
          (const fast_data_spi_payload_t *)payload;
      for (int i = 0; i < FAST_SAMPLES_PER_SPI_PKT; i++) {
        if (xQueueSend(s_fast_queue, &batch->samples[i], 0) != pdPASS) {
          s_stats.drop_fast++;
        }
      }
    }
    s_tp_fast_count++;
    break;

  case PKT_TYPE_SLOW_DATA:
    if (s_slow_queue == NULL || hdr->length != sizeof(slow_data_payload_t)) {
      break;
    }
    if (s_seq_slow_init) {
      if (hdr->sequence == s_last_seq_slow) {
        s_stats.dup_slow++;
        break;
      }
      s_stats.lost_slow += seq_gap(s_last_seq_slow, hdr->sequence);
    }
    s_last_seq_slow = hdr->sequence;
    s_seq_slow_init = true;

    if (xQueueSend(s_slow_queue, payload, 0) != pdPASS) {
      s_stats.drop_slow++;
    }
    break;

  default:
    break;
  }
}

static void tsk_spi_link_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea SPI Link Maestro Iniciada (DRDY por ISR, GPIO%d)",
           PIN_SPI_DRDY);

  // Publicamos el handle antes de registrar la ISR para que no exista una
  // ventana en la que la interrupción dispare sin destinatario.
  s_link_task = xTaskGetCurrentTaskHandle();

  spi_device_handle_t spi_dev = spi_manager_get_master_handle();
  if (spi_dev == NULL) {
    ESP_LOGE(TAG, "Handle SPI Maestro no disponible!");
    s_link_task = NULL;
    vTaskDelete(NULL);
    return;
  }

  // DRDY: entrada con pull-down (idle = LOW) e interrupción por flanco
  // ascendente.
  gpio_config_t drdy_cfg = {
      .pin_bit_mask = (1ULL << PIN_SPI_DRDY),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_POSEDGE,
  };
  gpio_config(&drdy_cfg);

  // El servicio de ISR de GPIO lo instala master_logic_start() una única vez.
  // Instalarlo acá también hace que el driver logee un error a nivel E cada
  // vez que se lo llama de más, aunque el resultado sea correcto.
  esp_err_t isr_ret = gpio_isr_handler_add(PIN_SPI_DRDY, drdy_isr_handler, NULL);
  if (isr_ret != ESP_OK) {
    ESP_LOGE(TAG, "No se pudo registrar la ISR de DRDY: %s",
             esp_err_to_name(isr_ret));
  }

  ESP_LOGI(TAG, "MAX_PKT_LEN=%d, SPI_RX_BUF_SIZE=%d, DRDY=GPIO%d",
           (int)MAX_PKT_LEN, (int)SPI_RX_BUF_SIZE, PIN_SPI_DRDY);

  int drdy_init = gpio_get_level(PIN_SPI_DRDY);
  ESP_LOGI(TAG, "DRDY estado inicial: %s",
           drdy_init ? "HIGH (Slave ya tiene datos?)" : "LOW (OK, esperado)");

  spi_transaction_t t;

  supervisor_register(TASK_ID_SPI_LINK, "spi_link", 5000);

  ESP_LOGI(TAG, "Esperando 500 ms para que el Slave arranque...");
  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGI(TAG, "Enviando Trama Despertador al Esclavo...");
  poke_slave(spi_dev);

  // Descartar los flancos acumulados durante el arranque para que el conteo
  // arranque alineado con el primer paquete real.
  ulTaskNotifyTake(pdTRUE, 0);

  while (1) {
    // ================================================================
    // PASO 1: Esperar el flanco de DRDY (bloqueante, 0% CPU)
    // ================================================================
    if (!wait_drdy_edge(spi_dev)) {
      continue;
    }

    // ================================================================
    // PASO 2: Transacción SPI (un paquete completo)
    // ================================================================
    s_stats.tx_total++;

    memset(&t, 0, sizeof(t));
    t.length = SPI_RX_BUF_SIZE * 8;
    t.tx_buffer = NULL;
    t.rx_buffer = rx_buffer;

    esp_err_t ret = spi_device_transmit(spi_dev, &t);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error SPI: %s", esp_err_to_name(ret));
      continue;
    }

    // Micro-gap: spi_device_transmit() ya tiene ~20 µs de overhead al
    // retornar, de sobra para que el esclavo vea CS HIGH y baje DRDY.
    esp_rom_delay_us(2);

    // ================================================================
    // PASO 3: Buscar paquete válido (Magic + CRC)
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
      s_stats.pkt_ok++;
      supervisor_checkin(TASK_ID_SPI_LINK);
      dispatch_packet(hdr, &rx_buffer[i + sizeof(fdr_header_t)]);

      found = true;
      i += data_len + 4 - 1;
    }

    if (!found) {
      s_stats.pkt_bad++;
    }

    // No se espera a ver DRDY en LOW: el flanco ascendente siguiente es lo que
    // delimita el próximo paquete (ver wait_drdy_edge).

    // Log de throughput cada 5 segundos
    int64_t now_us = esp_timer_get_time();
    if (s_tp_last_us == 0) s_tp_last_us = now_us;
    if (now_us - s_tp_last_us >= 5000000) {
      float rate = (float)s_tp_fast_count /
                   ((float)(now_us - s_tp_last_us) / 1000000.0f);
      ESP_LOGI(TAG,
               "THROUGHPUT: %.0f fast pkt/s | tx=%lu ok=%lu bad=%lu | "
               "perdidos f=%lu s=%lu | repetidos f=%lu s=%lu | "
               "descartados f=%lu s=%lu | drdy_to=%lu drdy_stale=%lu",
               rate, (unsigned long)s_stats.tx_total,
               (unsigned long)s_stats.pkt_ok, (unsigned long)s_stats.pkt_bad,
               (unsigned long)s_stats.lost_fast,
               (unsigned long)s_stats.lost_slow,
               (unsigned long)s_stats.dup_fast,
               (unsigned long)s_stats.dup_slow,
               (unsigned long)s_stats.drop_fast,
               (unsigned long)s_stats.drop_slow,
               (unsigned long)s_stats.drdy_timeouts,
               (unsigned long)s_stats.drdy_stale);
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
  // Prioridad alta pero no máxima: la tarea ahora duerme esperando la ISR de
  // DRDY, así que no necesita monopolizar el planificador del core 1.
  BaseType_t ok = xTaskCreatePinnedToCore(
      tsk_spi_link_master, "tsk_spi_link",
      4096, NULL,
      12,
      NULL,
      1);  // Core 1, aislado de la escritura a SD
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea SPI Link Maestro");
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
