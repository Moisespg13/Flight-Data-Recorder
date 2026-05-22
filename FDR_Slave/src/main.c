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
 *   Prio 6 - tsk_acq_slow_slave:     Lee el ADC (AD4131) vía SPI0
 *                                     y deposita los datos en q_slow_data.
 *   Prio 6 - tsk_spi_link_slave:     Recibe datos de ambas colas, empaqueta
 *                                     con Header + CRC, y transmite al Maestro
 *                                     vía SPI1 (Slave mode).
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
#include "pico/stdlib.h"

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

// Mutex para acceso exclusivo al bus SPI0 (AD7606 vs AD4131)
static SemaphoreHandle_t spi0_mutex = NULL;

// Handles de las tareas (para que el coordinator las despierte)
static TaskHandle_t h_tsk_acq_fast = NULL;
static TaskHandle_t h_tsk_acq_slow = NULL;
static TaskHandle_t h_tsk_spi_link = NULL;
static TaskHandle_t h_tsk_time_sync = NULL;

// Profundidad de las colas
#define FAST_DATA_QUEUE_DEPTH 4
#define SLOW_DATA_QUEUE_DEPTH 2

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
// Tarea: Coordinador (Prioridad 8 - MÁXIMA)
// ============================================================================
/**
 * @brief Primera tarea en arrancar. Espera la señal SYNC_TRIG del Maestro.
 *
 * Cuando recibe la señal:
 *   1. Despierta a todas las tareas de trabajo.
 *   2. Se autodestruye (libera su stack y deja de consumir recursos).
 */
static void tsk_coordinator_slave(void *pvParameters) {
  (void)pvParameters;

  // Configura pin SYNC_TRIG como entrada con pull-down
  gpio_init(PIN_SYNC_TRIG);
  gpio_set_dir(PIN_SYNC_TRIG, GPIO_IN);
  gpio_pull_down(PIN_SYNC_TRIG);

  printf("[COORD] Esperando SYNC_TRIG en GP%d...\n", PIN_SYNC_TRIG);

  // Espera a que el Maestro ponga SYNC_TRIG en alto
  while (gpio_get(PIN_SYNC_TRIG) == 0) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  printf("[COORD] *** SYNC recibido! Despertando tareas... ***\n");

  // Despierta a todas las tareas bloqueadas (Task Notification)
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
 */
static void tsk_acq_fast_slave(void *pvParameters) {
  (void)pvParameters;

  // Bloquearse hasta que el coordinator nos despierte
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  printf("[ACQ_FAST] *** Arrancando adquisición a %d canales. ***\n",
         FAST_SIGNALS_TOTAL_CH);

  fast_data_payload_t payload;
  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (true) {
    memset(&payload, 0, sizeof(payload));

    // Iniciar conversión (no necesita el bus SPI, solo pulsa CONVST)
    fast_signals_start_conversion();

    // Leer datos (toma el mutex SPI0 internamente)
    if (fast_signals_read(payload.channels) == 0) {
      payload.timestamp_ns = get_calibrated_timestamp_ns();

      // Enviar a la cola (sin esperar si está llena, descarta)
      if (xQueueSend(q_fast_data, &payload, 0) != pdPASS) {
        // TODO: Contar drops en producción.
      }
    }

    // Período de muestreo (~100 Hz por ahora)
    // TODO: Ajustar a 3000 SPS cuando el hardware esté calibrado
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
  }
}

// ============================================================================
// Tarea: Adquisición Lenta (Prioridad 6 - ALTA)
// ============================================================================
/**
 * @brief Lee datos del ADC AD4131 y los deposita en q_slow_data.
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

    // Leer los 16 canales lentos (toma el mutex SPI0 internamente)
    if (slow_signals_read(payload.channels) == 0) {
      payload.timestamp_ns = get_calibrated_timestamp_ns();

      // Enviar a la cola
      if (xQueueSend(q_slow_data, &payload, 0) != pdPASS) {
        // TODO: Contar drops.
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
 * Prioriza los datos rápidos: primero intenta vaciar q_fast_data,
 * luego revisa q_slow_data si no hay datos rápidos pendientes.
 */
static void tsk_spi_link_slave(void *pvParameters) {
  (void)pvParameters;

  // Bloquearse hasta que el coordinator nos despierte
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  printf("[SPI_LINK] Tarea de transmisión activa. Esperando datos...\n");

  fast_data_payload_t fast_payload;
  slow_data_payload_t slow_payload;

  while (true) {
    bool sent_something = false;

    // 1. Intentar datos rápidos (sin espera bloqueante)
    if (xQueueReceive(q_fast_data, &fast_payload, 0) == pdPASS) {
      spi_link_slave_send_fast(&fast_payload);
      sent_something = true;
    }

    // 2. Intentar datos lentos (siempre se revisa, evita starvation)
    if (xQueueReceive(q_slow_data, &slow_payload, 0) == pdPASS) {
      spi_link_slave_send_slow(&slow_payload);
      sent_something = true;
    }

    // 3. Si no había nada, dormir 1ms para no quemar CPU
    if (!sent_something) {
      vTaskDelay(pdMS_TO_TICKS(1));
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
 * Por ahora, simplemente detecta los flancos del PPS y logea
 * la diferencia para calibración futura (FR-T1, FR-T3).
 *
 * TODO: Implementar disciplining real del reloj (ajuste de offset/drift).
 */
static void tsk_time_sync_slave(void *pvParameters) {
  (void)pvParameters;

  // Bloquearse hasta que el coordinator nos despierte
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  // Configurar pin PPS como entrada
  gpio_init(PIN_PPS);
  gpio_set_dir(PIN_PPS, GPIO_IN);
  gpio_pull_down(PIN_PPS);

  printf("[TIME_SYNC] Reloj disciplinado por PPS activo en GP%d.\n", PIN_PPS);

  bool prev_pps = false;
  uint64_t last_pps_local_us = 0;  // time_us_64() en el PPS anterior
  uint32_t pps_count = 0;          // Contador de pulsos recibidos

  while (true) {
    bool current_pps = gpio_get(PIN_PPS);

    // Detectar flanco ascendente del PPS
    if (current_pps && !prev_pps) {
      uint64_t now_local_us = time_us_64();
      pps_count++;

      if (last_pps_local_us > 0) {
        // Tiempo local transcurrido entre dos PPS consecutivos
        uint64_t delta_local_us = now_local_us - last_pps_local_us;

        // El período real es exactamente 1,000,000 µs (1 segundo GPS).
        // La diferencia es el error acumulado del oscilador local.
        int32_t error_us = (int32_t)(delta_local_us - 1000000);

        // Corregir el offset: si el reloj local es rápido (+error),
        // restamos; si es lento (-error), sumamos.
        s_pps_offset_us -= error_us;
        s_pps_calibrated = true;

        // Log cada 10 segundos (no spamear)
        if ((pps_count % 10) == 0) {
          printf("[TIME_SYNC] PPS #%lu | drift=%ld us | offset=%lld us\n",
                 (unsigned long)pps_count, (long)error_us,
                 (long long)s_pps_offset_us);
        }
      } else {
        // Primer PPS: solo registrar el instante, no podemos calcular drift
        printf("[TIME_SYNC] Primer PPS recibido. Calibrando...\n");
      }

      last_pps_local_us = now_local_us;
    }

    prev_pps = current_pps;

    // Polling a 1 kHz
    // NOTA: El jitter del polling (~1ms) NO afecta la precisión a largo
    // plazo porque el offset se acumula correctamente. Solo afecta la
    // latencia de detección del flanco. Para <1µs de jitter, migrar
    // a interrupción GPIO (mejora futura con PIO).
    vTaskDelay(pdMS_TO_TICKS(1));
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

  // 3. Inicializar hardware de adquisición lenta (AD4131 vía SPI0)
  printf("[MAIN] Inicializando AD4131 (%d canales)...\n",
         SLOW_SIGNALS_NUM_CHANNELS);
  slow_signals_init(spi0_mutex);
  printf("[MAIN] AD4131 listo.\n");

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

  // 6. Crear tareas de trabajo (arrancan bloqueadas esperando al coordinator)
  xTaskCreate(tsk_acq_fast_slave, "acq_fast", 1024, NULL, 7, &h_tsk_acq_fast);

  xTaskCreate(tsk_acq_slow_slave, "acq_slow", 1024, NULL, 6, &h_tsk_acq_slow);

  xTaskCreate(tsk_spi_link_slave, "spi_link", 1024, NULL, 6, &h_tsk_spi_link);

  xTaskCreate(tsk_time_sync_slave, "time_sync", 768, NULL, 6, &h_tsk_time_sync);

  // Coordinator: prioridad máxima (8), se crea última
  xTaskCreate(tsk_coordinator_slave, "coordinator", 768, NULL, 8, NULL);

  printf("[MAIN] 5 tareas creadas. Arrancando scheduler de FreeRTOS...\n");

  // 7. Arrancar el scheduler (nunca retorna)
  vTaskStartScheduler();

  // Si llegamos acá, algo salió muy mal
  printf("[MAIN] *** ERROR: El scheduler retornó! ***\n");
  for (;;) {
    tight_loop_contents();
  }

  return 0;
}
