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
 *   Prio 6 - tsk_spi_link_slave:     Recibe datos de la cola, empaqueta con
 *                                     Header + CRC, y los transmite al Maestro
 *                                     vía SPI1 (Slave mode).
 *
 * La comunicación entre tareas se realiza mediante una QueueHandle_t
 * que transporta estructuras fast_data_payload_t.
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
#include "task.h"

// Proyecto
#include "crc_utils.h"
#include "fast_signals.h"
#include "pin_config.h"
#include "protocol.h"
#include "spi_link_slave.h"

// ============================================================================
// Handles globales
// ============================================================================

// Cola de datos de adquisición rápida
static QueueHandle_t q_fast_data = NULL;

// Handles de las tareas (necesarios para que el coordinator las despierte)
static TaskHandle_t h_tsk_acq_fast   = NULL;
static TaskHandle_t h_tsk_spi_link   = NULL;

// Profundidad de la cola: cuántas lecturas pueden estar en espera.
#define FAST_DATA_QUEUE_DEPTH  4

// ============================================================================
// Tarea: Coordinador (Prioridad 8 - MÁXIMA)
// ============================================================================
/**
 * @brief Primera tarea en arrancar. Espera la señal SYNC_TRIG del Maestro.
 *
 * Cuando recibe la señal:
 *   1. Despierta a tsk_acq_fast_slave y tsk_spi_link_slave.
 *   2. Se autodestruye (libera su stack y deja de consumir recursos).
 */
static void tsk_coordinator_slave(void *pvParameters) {
    (void)pvParameters;

    // Configurar pin SYNC_TRIG como entrada con pull-down
    gpio_init(PIN_SYNC_TRIG);
    gpio_set_dir(PIN_SYNC_TRIG, GPIO_IN);
    gpio_pull_down(PIN_SYNC_TRIG);

    printf("[COORD] Esperando SYNC_TRIG en GP%d...\n", PIN_SYNC_TRIG);

    // Esperar a que el Maestro ponga SYNC_TRIG en alto
    while (gpio_get(PIN_SYNC_TRIG) == 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    printf("[COORD] *** SYNC recibido! Despertando tareas... ***\n");

    // Despertar a las tareas bloqueadas (Task Notification)
    xTaskNotifyGive(h_tsk_acq_fast);
    xTaskNotifyGive(h_tsk_spi_link);

    printf("[COORD] Tareas notificadas. Autodestruyendo coordinator.\n");

    // Autodestrucción: liberar stack y CPU
    vTaskDelete(NULL);
}

// ============================================================================
// Tarea: Adquisición Rápida (Prioridad 7 - CRÍTICA)
// ============================================================================
/**
 * @brief Lee datos de los ADCs (AD7606) y los deposita en q_fast_data.
 *
 * Arranca bloqueada esperando la notificación del coordinator.
 * Una vez despertada, entra en un loop infinito de muestreo:
 *   - Inicia conversión del AD7606.
 *   - Lee los 16 canales (2 chips x 8 canales).
 *   - Empaqueta en fast_data_payload_t con timestamp.
 *   - Envía a la cola (sin esperar si está llena, descarta).
 */
static void tsk_acq_fast_slave(void *pvParameters) {
    (void)pvParameters;

    // Bloquearse hasta que el coordinator nos despierte
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    printf("[ACQ_FAST] *** Arrancando adquisición a %d canales. ***\n",
           FAST_SIGNALS_TOTAL_CH);

    // Loop de adquisición
    fast_data_payload_t payload;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (true) {
        memset(&payload, 0, sizeof(payload));

        // Iniciar conversión y leer datos del AD7606
        fast_signals_start_conversion();

        if (fast_signals_read(payload.channels) == 0) {
            // Capturar timestamp (us -> ns)
            payload.timestamp_ns = (uint64_t)time_us_64() * 1000;

            // Enviar a la cola (sin esperar si está llena, descarta)
            if (xQueueSend(q_fast_data, &payload, 0) != pdPASS) {
                // Cola llena: el dato se descartó.
                // TODO: Contar drops en producción.
            }
        }

        // Esperar hasta el próximo período de muestreo (~100 Hz por ahora)
        // TODO: Ajustar a la frecuencia real de muestreo (ej. 3000 SPS)
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(10));
    }
}

// ============================================================================
// Tarea: Enlace SPI con el Maestro (Prioridad 6 - ALTA)
// ============================================================================
/**
 * @brief Recibe datos de q_fast_data y los transmite al Maestro.
 *
 * Arranca bloqueada esperando la notificación del coordinator.
 * Una vez despertada, se bloquea esperando datos en la cola.
 * Cuando llega un payload, lo empaqueta con header + CRC y lo envía por SPI1.
 */
static void tsk_spi_link_slave(void *pvParameters) {
    (void)pvParameters;

    // Bloquearse hasta que el coordinator nos despierte
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    printf("[SPI_LINK] Tarea de transmisión activa. Esperando datos...\n");

    fast_data_payload_t payload;

    while (true) {
        // Bloquear hasta que haya datos disponibles (espera infinita)
        if (xQueueReceive(q_fast_data, &payload, portMAX_DELAY) == pdPASS) {
            // Transmitir al Maestro
            spi_link_slave_send(&payload);
        }
    }
}

// ============================================================================
// Hooks de FreeRTOS (obligatorios si están habilitados en FreeRTOSConfig.h)
// ============================================================================

/**
 * @brief Se llama si malloc de FreeRTOS falla (se acabó el heap).
 */
void vApplicationMallocFailedHook(void) {
    printf("[RTOS] *** ERROR: Malloc falló! Sin memoria. ***\n");
    for (;;) { tight_loop_contents(); }
}

/**
 * @brief Se llama si se detecta desbordamiento de stack en una tarea.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    printf("[RTOS] *** ERROR: Stack overflow en tarea '%s'! ***\n", pcTaskName);
    for (;;) { tight_loop_contents(); }
}

// ============================================================================
// Main
// ============================================================================
int main(void) {
    stdio_init_all();

    // Bucle de espera para que Windows reconozca el puerto COM tras el flasheo
    for (int i = 5; i > 0; i--) {
        printf("=== FDR ESCLAVO (RP2040) - Boot en %d seg ===\n", i);
        sleep_ms(1000);
    }

    printf("Protocol Magic: 0x%02X\n", FDR_PROTO_MAGIC);

    // 1. Inicializar hardware de adquisición (AD7606 x2 vía SPI0)
    printf("[MAIN] Inicializando AD7606 x%d (%d canales)...\n",
           FAST_SIGNALS_NUM_CHIPS, FAST_SIGNALS_TOTAL_CH);
    fast_signals_init();
    printf("[MAIN] AD7606 listo.\n");

    // 2. Inicializar enlace SPI (SPI1 como Slave)
    printf("[MAIN] Inicializando SPI Link Slave...\n");
    spi_link_slave_init();
    printf("[MAIN] SPI Link Slave listo.\n");

    // 3. Crear la cola de datos de adquisición rápida
    q_fast_data = xQueueCreate(FAST_DATA_QUEUE_DEPTH, sizeof(fast_data_payload_t));
    if (q_fast_data == NULL) {
        printf("[MAIN] *** ERROR: No se pudo crear q_fast_data! ***\n");
        for (;;) { tight_loop_contents(); }
    }
    printf("[MAIN] Cola q_fast_data creada (profundidad: %d).\n",
           FAST_DATA_QUEUE_DEPTH);

    // 4. Crear tareas (las de trabajo arrancan bloqueadas esperando al coordinator)
    xTaskCreate(
        tsk_acq_fast_slave,
        "acq_fast",         // Nombre para debug
        1024,               // Stack: 1024 words = 4 KB
        NULL,               // Parámetro
        7,                  // Prioridad 7 (Crítica)
        &h_tsk_acq_fast     // Handle (para que el coordinator la despierte)
    );

    xTaskCreate(
        tsk_spi_link_slave,
        "spi_link",         // Nombre para debug
        1024,               // Stack: 4 KB
        NULL,
        6,                  // Prioridad 6 (Alta)
        &h_tsk_spi_link     // Handle
    );

    // Coordinator: se crea última pero tiene prioridad máxima (8)
    xTaskCreate(
        tsk_coordinator_slave,
        "coordinator",      // Nombre para debug
        512,                // Stack: 2 KB (solo espera un GPIO, no necesita mucho)
        NULL,
        8,                  // Prioridad 8 (Máxima)
        NULL                // No necesitamos su handle
    );

    printf("[MAIN] Tareas creadas. Arrancando scheduler de FreeRTOS...\n");

    // 5. Arrancar el scheduler (nunca retorna si todo sale bien)
    vTaskStartScheduler();

    // Si llegamos acá, algo salió muy mal
    printf("[MAIN] *** ERROR: El scheduler retornó! ***\n");
    for (;;) { tight_loop_contents(); }

    return 0;
}
