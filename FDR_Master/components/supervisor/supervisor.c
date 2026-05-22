
/**
 * @file supervisor.c
 * @brief Implementación del módulo Supervisor (Watchdog por software).
 *
 * Mantiene una tabla interna con el estado de cada tarea monitoreada:
 *   - Nombre (para logs)
 *   - Timeout máximo (ms)
 *   - Timestamp del último check-in (µs)
 *   - Flag de si está registrada
 *
 * Cuando supervisor_check_all() detecta que una tarea excedió su
 * timeout, logea un ERROR con el nombre de la tarea y cuántos
 * milisegundos lleva "muerta".
 *
 * Las funciones register y checkin son thread-safe porque:
 *   - register se llama solo al inicio (antes del scheduler o
 *     en la primera iteración de cada tarea)
 *   - checkin escribe un solo int64_t (operación atómica en ESP32)
 */

#include "supervisor.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "SUPERVISOR";

// Información de cada tarea monitoreada
typedef struct {
  const char *name;         // Nombre legible
  uint32_t timeout_ms;      // Máximo tiempo sin check-in
  int64_t last_checkin_us;  // Último check-in (µs, esp_timer)
  bool registered;          // ¿Está activa?
  bool alarm_reported;      // ¿Ya se reportó la alarma? (evitar spam)
} task_monitor_t;

static task_monitor_t s_tasks[TASK_ID_MAX] = {0};

void supervisor_register(supervisor_task_id_t id, const char *name,
                         uint32_t timeout_ms) {
  if (id >= TASK_ID_MAX) {
    return;
  }

  s_tasks[id].name = name;
  s_tasks[id].timeout_ms = timeout_ms;
  s_tasks[id].last_checkin_us = esp_timer_get_time();
  s_tasks[id].registered = true;
  s_tasks[id].alarm_reported = false;

  ESP_LOGI(TAG, "Registrada: [%s] timeout=%lu ms", name,
           (unsigned long)timeout_ms);
}

void supervisor_checkin(supervisor_task_id_t id) {
  if (id >= TASK_ID_MAX) {
    return;
  }

  s_tasks[id].last_checkin_us = esp_timer_get_time();

  // Si estaba en alarma, la tarea se recuperó
  if (s_tasks[id].alarm_reported) {
    ESP_LOGW(TAG, "[%s] RECUPERADA (volvió a reportarse)",
             s_tasks[id].name);
    s_tasks[id].alarm_reported = false;
  }
}

int supervisor_check_all(void) {
  int64_t now_us = esp_timer_get_time();
  int alarms = 0;

  for (int i = 0; i < TASK_ID_MAX; i++) {
    if (!s_tasks[i].registered) {
      continue;
    }

    int64_t elapsed_us = now_us - s_tasks[i].last_checkin_us;
    int64_t timeout_us = (int64_t)s_tasks[i].timeout_ms * 1000;

    if (elapsed_us > timeout_us) {
      alarms++;

      // Solo loguear la primera vez (evitar llenar la consola)
      if (!s_tasks[i].alarm_reported) {
        int64_t dead_ms = elapsed_us / 1000;
        ESP_LOGE(TAG,
                 "*** ALARMA: [%s] no responde! "
                 "Último check-in hace %lld ms (timeout: %lu ms) ***",
                 s_tasks[i].name, (long long)dead_ms,
                 (unsigned long)s_tasks[i].timeout_ms);
        s_tasks[i].alarm_reported = true;
      }
    }
  }

  return alarms;
}
