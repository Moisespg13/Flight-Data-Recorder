
/**
 * @file supervisor.h
 * @brief API del módulo Supervisor (Watchdog por software).
 *
 * Implementa un sistema de "check-in" donde cada tarea del Maestro
 * se reporta periódicamente como viva. Si una tarea no se reporta
 * dentro de su timeout, el Supervisor genera una alarma.
 *
 * Uso:
 *   1. Al arrancar, cada tarea llama a supervisor_register()
 *   2. Dentro de su loop, cada tarea llama a supervisor_checkin()
 *   3. tsk_supervisor_master llama a supervisor_check_all() periódicamente
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// IDs de las tareas monitoreadas
typedef enum {
  TASK_ID_WRITER = 0,
  TASK_ID_TIME_SYNC,
  TASK_ID_DATA_PROC,
  TASK_ID_SLOW_PROC,
  TASK_ID_DISCRETE,
  TASK_ID_SPI_LINK,
  TASK_ID_IMU,
  TASK_ID_MAX // Siempre al final (indica la cantidad total)
} supervisor_task_id_t;

/**
 * @brief Registra una tarea para ser monitoreada.
 *
 * @param id         ID único de la tarea (del enum).
 * @param name       Nombre legible para los logs (ej: "writer").
 * @param timeout_ms Tiempo máximo permitido sin check-in (milisegundos).
 *                   Si la tarea no se reporta en este tiempo, se genera alarma.
 */
void supervisor_register(supervisor_task_id_t id, const char *name,
                         uint32_t timeout_ms);

/**
 * @brief La tarea reporta que está viva.
 *
 * Debe llamarse desde el loop principal de cada tarea, idealmente
 * en cada iteración. Actualiza el timestamp de último check-in.
 *
 * @param id ID de la tarea que se reporta.
 */
void supervisor_checkin(supervisor_task_id_t id);

/**
 * @brief Verifica el estado de todas las tareas registradas.
 *
 * Compara el tiempo actual con el último check-in de cada tarea.
 * Si alguna excedió su timeout, logea un error.
 *
 * @return Cantidad de tareas en estado de alarma (0 = todo OK).
 */
int supervisor_check_all(void);
