/**
 * @file main.c
 * @brief Punto de entrada del firmware Esclavo (RP2040-Zero) con FreeRTOS.
 *
 * Arquitectura de tareas (prioridades según roadmap Notion):
 *
 *   Prio 8 - tsk_coordinator_slave:  Espera SYNC_TRIG del Maestro, despierta
 *                                     a las demás tareas, y se autodestruye.
 *   Prio 7 - tsk_acq_fast_slave:     Lee los ADCs (AD7606) vía SPI0
 *                                     y deposita los datos en q_fast_data.
 *   Prio 6 - tsk_acq_slow_slave:     Lee el ADC (AD7490) vía SPI0
 *                                     y deposita los datos en q_slow_data.
 *   Prio 6 - tsk_spi_link_slave:     Recibe datos de ambas colas, empaqueta
 *                                     con Header + CRC, y transmite al Maestro
 *                                     por PIO (SPI slave por software, ver
 *                                     spi_slave_tx.pio).
 *   Prio 6 - tsk_time_sync_slave:    Recalibra el timestamp local usando
 *                                     el pulso PPS del Maestro/GNSS.
 *
 * Comunicación inter-tarea:
 *   - q_fast_data: fast_data_payload_t (acq_fast -> spi_link)
 *   - q_slow_data: slow_data_payload_t (acq_slow -> spi_link)
 *   - spi0_mutex:  Mutex para coordinar acceso a SPI0 (fast vs slow)
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/time.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

// Proyecto
#include "crc_utils.h"
#include "fast_signals.h"
#include "pin_config.h"
#include "protocol.h"
#include "slow_signals.h"
#include "spi_link_slave.h"

// ============================================================================
// Handles globales
// ============================================================================

// Colas de datos
static QueueHandle_t q_fast_data = NULL;
static QueueHandle_t q_slow_data = NULL;

// Mutex para acceso exclusivo al bus SPI0 (AD7606 vs AD7490)
static SemaphoreHandle_t spi0_mutex = NULL;

// Handles de las tareas (para que el coordinator las despierte)
static TaskHandle_t h_tsk_coordinator = NULL;
static TaskHandle_t h_tsk_acq_fast = NULL;
static TaskHandle_t h_tsk_acq_slow = NULL;
static TaskHandle_t h_tsk_spi_link = NULL;
static TaskHandle_t h_tsk_time_sync = NULL;

// Profundidad de las colas
#define FAST_DATA_QUEUE_DEPTH 64
#define SLOW_DATA_QUEUE_DEPTH 2

// Período del timer de muestreo rápido (µs)
// 333 µs ≈ 3003 Hz (el entero más cercano a 3000 Hz)
#define FAST_SAMPLE_PERIOD_US 333

// Contador de drops (muestras descartadas por cola llena)
static volatile uint32_t s_fast_drops = 0;
static volatile uint32_t s_slow_drops = 0;

// ============================================================================
// Reloj Disciplinado por PPS
// ============================================================================
// El RP2040 no tiene RTC ni conoce la hora UTC. Solo tiene un contador
// monotónico (time_us_64). El PPS del GNSS llega exactamente 1 vez por
// segundo. Usamos cada flanco PPS para calcular un offset que alinea
// nuestro contador local con los segundos UTC.
//
// Modelo: timestamp_calibrado_us = time_us_64() + s_pps_offset_us
//
// En cada PPS:
//   s_pps_offset_us += (1,000,000 - delta_local)
//   donde delta_local = time_us_64_ahora - time_us_64_pps_anterior
//
// Si el reloj local es perfecto, delta_local = 1,000,000 y offset no cambia.
// Si el oscilador del RP2040 tiene drift (ej: +50 ppm), cada segundo local
// dura 1,000,050 µs, y el offset se corrige en -50 µs por pulso.

static volatile int64_t s_pps_offset_us = 0;   // Offset acumulado
static volatile bool s_pps_calibrated = false;  // true después del 2do PPS

/**
 * @brief Obtiene un timestamp calibrado por PPS en nanosegundos.
 *
 * Las tareas de adquisición deben usar esta función en vez de
 * time_us_64() * 1000 para obtener timestamps sincronizados con el GPS.
 *
 * @return Timestamp en nanosegundos, alineado al PPS.
 */
static inline uint64_t get_calibrated_timestamp_ns(void) {
  int64_t now_us = (int64_t)time_us_64();
  int64_t calibrated_us = now_us + s_pps_offset_us;
  return (uint64_t)calibrated_us * 1000ULL;
}

// ============================================================================
// ISR: Timer de muestreo rápido (cada 333 µs → ~3003 Hz)
// ============================================================================
static repeating_timer_t s_fast_timer;

static bool fast_timer_callback(repeating_timer_t *rt) {
  (void)rt;
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveIndexedFromISR(h_tsk_acq_fast, 1, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  return true; // Seguir repitiendo
}

// ============================================================================
// ISR: Callback compartido para GPIO (PPS + SYNC_TRIG)
// ============================================================================
// El SDK del RP2040 permite un solo callback GPIO por core. Este callback
// despacha por número de pin.
static volatile uint64_t s_pps_edge_us = 0;

static void shared_gpio_callback(uint gpio, uint32_t events) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (gpio == PIN_LINK_CS && (events & GPIO_IRQ_EDGE_FALL)) {
    if (h_tsk_coordinator != NULL) {
      vTaskNotifyGiveFromISR(h_tsk_coordinator, &xHigherPriorityTaskWoken);
    }
  }

  if (gpio == PIN_LINK_CS && (events & GPIO_IRQ_EDGE_RISE)) {
    gpio_put(PIN_DRDY, 0); // Bajar DRDY rápido para handshake
    if (h_tsk_spi_link != NULL) {
      vTaskNotifyGiveIndexedFromISR(h_tsk_spi_link, 2, &xHigherPriorityTaskWoken);
    }
  }

  if (gpio == PIN_PPS && (events & GPIO_IRQ_EDGE_RISE)) {
    s_pps_edge_us = time_us_64(); // Captura con < 1 µs de jitter
    if (h_tsk_time_sync != NULL) {
      vTaskNotifyGiveIndexedFromISR(h_tsk_time_sync, 1,
                                    &xHigherPriorityTaskWoken);
    }
  }

  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ============================================================================
// Tarea: Coordinador (Prioridad 8 - MÁXIMA)
// ============================================================================
/**
 * @brief Primera tarea en arrancar. Espera la señal SYNC_TRIG del Maestro.
 *
 * Usa interrupción GPIO para detección inmediata (sin polling).
 * Cuando recibe la señal:
 *   1. Despierta a todas las tareas de trabajo.
 *   2. Se autodestruye (libera su stack y deja de consumir recursos).
 */
static void tsk_coordinator_slave(void *pvParameters) {
  (void)pvParameters;

  // Configurar interrupción para el flanco de bajada y subida del CS
  // La subida la maneja spi_link, pero la bajada despertará a este coordinador
  gpio_set_irq_enabled_with_callback(PIN_LINK_CS, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &shared_gpio_callback);

  printf("[COORD] Esperando Trama SPI Despertador (CS LOW) en GP%d...\n", PIN_LINK_CS);
  // Bloquearse hasta que la ISR nos notifique
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  printf("[COORD] *** SYNC recibido! Despertando tareas... ***\n");

  // Deshabilitar IRQ de bajada (ya no la necesitamos), dejamos la de subida para spi_link
  gpio_set_irq_enabled(PIN_LINK_CS, GPIO_IRQ_EDGE_FALL, false);
  h_tsk_coordinator = NULL; // Prevenir que la ISR notifique a tarea eliminada

  // Despierta a todas las tareas bloqueadas (Task Notification, index 0)
  xTaskNotifyGive(h_tsk_acq_fast);
  xTaskNotifyGive(h_tsk_acq_slow);
  xTaskNotifyGive(h_tsk_spi_link);
  xTaskNotifyGive(h_tsk_time_sync);

  printf("[COORD] Tareas notificadas. Autodestruyendo coordinator.\n");

  // Autodestrucción: libera stack y CPU
  vTaskDelete(NULL);
}

// ============================================================================
// Tarea: Adquisición Rápida (Prioridad 7 - CRÍTICA)
// ============================================================================
/**
 * @brief Lee datos de los ADCs (AD7606) y los deposita en q_fast_data.
 *        Arranca bloqueada hasta que el coordinator la despierte.
 *
 * Usa un timer de hardware (repeating_timer) para muestrear a ~3003 Hz.
 * El timer notifica la tarea vía Task Notification (index 1).
 */
static void tsk_acq_fast_slave(void *pvParameters) {
  (void)pvParameters;

  // Bloquearse hasta que el coordinator nos despierte (index 0)
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  printf("[ACQ_FAST] *** Arrancando adquisición a %d canales a ~3 kHz. ***\n",
         FAST_SIGNALS_TOTAL_CH);

  // Iniciar timer de hardware para muestreo a 3 kHz
  // Valor negativo = intervalo exacto desde inicio (no desde fin)
  if (!add_repeating_timer_us(-FAST_SAMPLE_PERIOD_US, fast_timer_callback,
                               NULL, &s_fast_timer)) {
    printf("[ACQ_FAST] *** ERROR: No se pudo crear timer! ***\n");
    vTaskDelete(NULL);
    return;
  }
  printf("[ACQ_FAST] Timer de muestreo iniciado (período=%d µs).\n",
         FAST_SAMPLE_PERIOD_US);

  fast_data_payload_t payload;

  while (true) {
    // Bloquearse hasta que el timer ISR notifique (index 1)
    ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);

    memset(&payload, 0, sizeof(payload));

    // Iniciar conversión (no necesita el bus SPI, solo pulsa CONVST)
    fast_signals_start_conversion();

    // Leer datos a un buffer intermedio para evitar unaligned pointer warning
    int16_t temp_channels[FAST_SIGNALS_TOTAL_CH];
    if (fast_signals_read(temp_channels) == 0) {
      memcpy(payload.channels, temp_channels, sizeof(temp_channels));
      payload.timestamp_ns = get_calibrated_timestamp_ns();

      // Enviar a la cola (sin esperar si está llena, descarta)
      if (xQueueSend(q_fast_data, &payload, 0) != pdPASS) {
        s_fast_drops++;
        if (s_fast_drops % 1000 == 0) {
          printf("[ACQ_FAST] WARN: %lu drops acumulados.\n",
                 (unsigned long)s_fast_drops);
        }
      }
    }
  }
}

// ============================================================================
// Tarea: Adquisición Lenta (Prioridad 6 - ALTA)
// ============================================================================
/**
 * @brief Lee datos del ADC AD7490 y los deposita en q_slow_data.
 *        Arranca bloqueada hasta que el coordinator la despierte.
 *
 * Muestrea los 16 canales lentos a ~20 Hz (FR-A2).
 */
static void tsk_acq_slow_slave(void *pvParameters) {
  (void)pvParameters;

  // Bloquearse hasta que el coordinator nos despierte
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  printf("[ACQ_SLOW] *** Arrancando adquisición lenta (%d canales). ***\n",
         SLOW_SIGNALS_NUM_CHANNELS);

  slow_data_payload_t payload;
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (true) {
    memset(&payload, 0, sizeof(payload));

    // Leer los 16 canales lentos a un buffer intermedio
    int16_t temp_channels[SLOW_SIGNALS_NUM_CHANNELS];
    if (slow_signals_read(temp_channels) == 0) {
      memcpy(payload.channels, temp_channels, sizeof(temp_channels));
      payload.timestamp_ns = get_calibrated_timestamp_ns();

      // Enviar a la cola
      if (xQueueSend(q_slow_data, &payload, 0) != pdPASS) {
        s_slow_drops++;
        if (s_slow_drops % 100 == 0) {
          printf("[ACQ_SLOW] WARN: %lu drops acumulados.\n", (unsigned long)s_slow_drops);
        }
      }
    }

    // Período: 50 ms = 20 Hz (FR-A2)
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
  }
}

// ============================================================================
// Tarea: Enlace SPI con el Maestro (Prioridad 6 - ALTA)
// ============================================================================
/**
 * @brief Recibe datos de q_fast_data y q_slow_data y los transmite.
 *        Arranca bloqueada hasta que el coordinator la despierte.
 *
 * Optimizada para 3 kHz: drena TODOS los paquetes rápidos pendientes
 * antes de revisar los lentos. Bloquea en la cola rápida cuando no
 * hay datos, eliminando el delay fijo de 1 ms.
 */
static void tsk_spi_link_slave(void *pvParameters) {
  (void)pvParameters;

  // Bloquearse hasta que el coordinator nos despierte
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  printf("[SPI_LINK] Tarea de transmisión activa (optimizada para 3 kHz).\n");

  fast_data_payload_t fast_payload;
  slow_data_payload_t slow_payload;

  while (true) {
    // 1. Drenar TODOS los datos rápidos pendientes (prioridad máxima)
    while (xQueueReceive(q_fast_data, &fast_payload, 0) == pdPASS) {
      spi_link_slave_send_fast(&fast_payload);
    }

    // 2. Revisar datos lentos (uno por iteración, evita starvation)
    if (xQueueReceive(q_slow_data, &slow_payload, 0) == pdPASS) {
      spi_link_slave_send_slow(&slow_payload);
    } else {
      // 3. Nada pendiente → bloquear en cola rápida (ahorra CPU)
      if (xQueueReceive(q_fast_data, &fast_payload, pdMS_TO_TICKS(5))
          == pdPASS) {
        spi_link_slave_send_fast(&fast_payload);
      }
    }
  }
}

// ============================================================================
// Tarea: Sincronización Temporal (Prioridad 6 - ALTA)
// ============================================================================
/**
 * @brief Recalibra el timestamp local usando el pulso PPS del Maestro.
 *        Arranca bloqueada hasta que el coordinator la despierte.
 *
 * Usa interrupción GPIO para capturar el flanco PPS con < 1 µs de jitter.
 * La ISR captura time_us_64() y notifica esta tarea (index 1).
 * La tarea procesa el timestamp y corrige el drift del oscilador.
 */
static void tsk_time_sync_slave(void *pvParameters) {
  (void)pvParameters;

  // Bloquearse hasta que el coordinator nos despierte (index 0)
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  // GPIO ya configurado en main(). Limpiar notificaciones PPS espurias
  // que pudieran haberse acumulado antes del arranque.
  ulTaskNotifyTakeIndexed(1, pdTRUE, 0); // Non-blocking, limpia index 1

  printf("[TIME_SYNC] Reloj disciplinado por PPS (interrupción) en GP%d.\n",
         PIN_PPS);

  uint64_t last_pps_us = 0;  // Timestamp del PPS anterior (capturado por ISR)
  uint32_t pps_count = 0;

  while (true) {
    // Bloquearse hasta que la ISR de PPS notifique (index 1)
    ulTaskNotifyTakeIndexed(1, pdTRUE, portMAX_DELAY);

    // Leer el timestamp capturado por la ISR (< 1 µs de jitter)
    uint64_t edge_us = s_pps_edge_us;
    pps_count++;

    if (last_pps_us > 0) {
      // Tiempo local transcurrido entre dos PPS consecutivos
      uint64_t delta_local_us = edge_us - last_pps_us;

      // El período real es exactamente 1,000,000 µs (1 segundo GPS).
      int32_t error_us = (int32_t)(delta_local_us - 1000000);

      // Corregir el offset
      s_pps_offset_us -= error_us;
      s_pps_calibrated = true;

      // Log cada 10 segundos
      if ((pps_count % 10) == 0) {
        printf("[TIME_SYNC] PPS #%lu | drift=%ld us | offset=%lld us\n",
               (unsigned long)pps_count, (long)error_us,
               (long long)s_pps_offset_us);
      }
    } else {
      printf("[TIME_SYNC] Primer PPS recibido. Calibrando...\n");
    }

    last_pps_us = edge_us;
  }
}

// ============================================================================
// Tarea: Watchdog (Prioridad 1 - MUY BAJA)
// ============================================================================
/**
 * @brief Perro guardián por software/hardware.
 *        Prioridad muy baja. Si las tareas de adquisición o SPI (prioridades 6-8)
 *        entran en bucle infinito o se cuelgan, esta tarea sufrirá "starvation"
 *        y el micro se reseteará físicamente a los 2 segundos.
 */
static void tsk_watchdog_slave(void *pvParameters) {
  (void)pvParameters;
  while (true) {
    watchdog_update();
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ============================================================================
// Hooks de FreeRTOS
// ============================================================================

void vApplicationMallocFailedHook(void) {
  printf("[RTOS] *** ERROR: Malloc falló! Sin memoria. ***\n");
  for (;;) {
    tight_loop_contents();
  }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  printf("[RTOS] *** ERROR: Stack overflow en tarea '%s'! ***\n", pcTaskName);
  for (;;) {
    tight_loop_contents();
  }
}

// ============================================================================
// Main
// ============================================================================
int main(void) {
  stdio_init_all();

  // Bucle de espera para que Windows reconozca el puerto COM
  for (int i = 5; i > 0; i--) {
    printf("=== FDR ESCLAVO (RP2040) - Boot en %d seg ===\n", i);
    sleep_ms(1000);
  }

  printf("Protocol Magic: 0x%02X\n", FDR_PROTO_MAGIC);

  // 1. Crear mutex para SPI0 (ANTES de inicializar los drivers)
  spi0_mutex = xSemaphoreCreateMutex();
  if (spi0_mutex == NULL) {
    printf("[MAIN] *** ERROR: No se pudo crear spi0_mutex! ***\n");
    for (;;) {
      tight_loop_contents();
    }
  }
  printf("[MAIN] Mutex SPI0 creado.\n");

  // 2. Inicializar hardware de adquisición rápida (AD7606 x2 vía SPI0)
  printf("[MAIN] Inicializando AD7606 x%d (%d canales)...\n",
         FAST_SIGNALS_NUM_CHIPS, FAST_SIGNALS_TOTAL_CH);
  fast_signals_init(spi0_mutex);
  printf("[MAIN] AD7606 listo.\n");

  // 3. Inicializar hardware de adquisición lenta (AD7490 vía SPI0)
  printf("[MAIN] Inicializando AD7490 (%d canales)...\n",
         SLOW_SIGNALS_NUM_CHANNELS);
  slow_signals_init(spi0_mutex);
  printf("[MAIN] AD7490 listo.\n");

  // 4. Inicializar enlace SPI (SPI1 como Slave)
  printf("[MAIN] Inicializando SPI Link Slave...\n");
  spi_link_slave_init();
  printf("[MAIN] SPI Link Slave listo.\n");

  // 5. Crear colas de datos
  q_fast_data =
      xQueueCreate(FAST_DATA_QUEUE_DEPTH, sizeof(fast_data_payload_t));
  q_slow_data =
      xQueueCreate(SLOW_DATA_QUEUE_DEPTH, sizeof(slow_data_payload_t));

  if (q_fast_data == NULL || q_slow_data == NULL) {
    printf("[MAIN] *** ERROR: No se pudieron crear las colas! ***\n");
    for (;;) {
      tight_loop_contents();
    }
  }
  printf("[MAIN] Colas creadas (fast:%d, slow:%d).\n", FAST_DATA_QUEUE_DEPTH,
         SLOW_DATA_QUEUE_DEPTH);

  // 5.5 Configurar interrupciones GPIO para PPS
  //     (debe hacerse desde core 0 antes del scheduler para SMP)
  gpio_init(PIN_PPS);
  gpio_set_dir(PIN_PPS, GPIO_IN);
  gpio_pull_down(PIN_PPS);

  // Registrar callback compartido y habilitar interrupciones
  gpio_set_irq_enabled_with_callback(PIN_PPS, GPIO_IRQ_EDGE_RISE, true,
                                      shared_gpio_callback);
  printf("[MAIN] Interrupciones GPIO configuradas (PPS=GP%d).\n", PIN_PPS);

  // 6. Crear tareas de trabajo (arrancan bloqueadas esperando al coordinator)
  xTaskCreate(tsk_acq_fast_slave, "acq_fast", 1024, NULL, 7, &h_tsk_acq_fast);

  xTaskCreate(tsk_acq_slow_slave, "acq_slow", 1024, NULL, 6, &h_tsk_acq_slow);

  xTaskCreate(tsk_spi_link_slave, "spi_link", 1024, NULL, 6, &h_tsk_spi_link);

  xTaskCreate(tsk_time_sync_slave, "time_sync", 768, NULL, 6, &h_tsk_time_sync);

  // Coordinator: prioridad máxima (8), guarda handle para la ISR
  xTaskCreate(tsk_coordinator_slave, "coordinator", 768, NULL, 8,
              &h_tsk_coordinator);

  // Tarea de Watchdog: prioridad mínima (1)
  xTaskCreate(tsk_watchdog_slave, "watchdog", 256, NULL, 1, NULL);

  // Habilitar Watchdog por hardware (2000 ms, pausar en debug)
  watchdog_enable(2000, 1);
  printf("[MAIN] Watchdog por hardware habilitado (2000 ms).\n");

  printf("[MAIN] 6 tareas creadas. Arrancando scheduler de FreeRTOS...\n");

  // 7. Arrancar el scheduler (nunca retorna)
  vTaskStartScheduler();

  // Si llegamos acá, algo salió muy mal
  printf("[MAIN] *** ERROR: El scheduler retornó! ***\n");
  for (;;) {
    tight_loop_contents();
  }

  return 0;
}
