
/**
 * @file master_logic.c
 * @brief Lógica principal del Maestro (ESP32-S3).
 *
 * Inicializa el enlace SPI para recibir datos del Esclavo (RP2040),
 * valida los paquetes (Magic + CRC), lee señales discretas directas,
 * controla la señal SYNC_TRIG para sincronizar al Esclavo, y lanza
 * las tareas de procesamiento y almacenamiento en MicroSD.
 *
 * Formato de archivo: Frames binarios CEV (16 bits por dato),
 * ensamblados según el formato de muestreo seleccionado (1, 2 o 3).
 *
 * Arquitectura de tareas del Maestro:
 *   Prio 12 - tsk_spi_link_master:    Recibe datos del Esclavo por SPI (core 1)
 *   Prio 9  - tsk_frame_assembler:    Ensambla frames CEV → anillo PSRAM (core 1)
 *   Prio 6  - tsk_sd_writer:          Vacía el anillo a las dos MicroSD (core 0)
 *   Prio 7 - tsk_time_sync_master:    Sincronización GPS/PPS
 *   Prio 5 - tsk_data_proc_master:    Procesa datos rápidos → buffer compartido
 *   Prio 5 - tsk_slow_proc_master:    Procesa datos lentos → buffer compartido
 *   Prio 5 - tsk_discrete_master:     Lee 16 entradas digitales → buffer compartido
 *   Prio 5 - tsk_imu_master:          Lee IMU XSENS → buffer compartido
 *   Prio 4 - tsk_cmd_port_master:     Puerto de Comandos RS-232
 *   Prio 3 - tsk_supervisor_master:   Watchdog por software
 */

#include "master_logic.h"
#include "cmd_port.h"
#include "crc_utils.h"
#include "discrete_signals.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gnss.h"
#include "imu_xsens.h"
#include "led_indicator.h"
#include "nvs.h"
#include "pin_config.h"
#include "rtc_ds3232.h"
#include "protocol.h"
#include "sample_format.h"
#include "sd_storage.h"
#include "supervisor.h"
#include "spi_link.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "MASTER_LOGIC";

// ============================================================================
// Colas de recepción SPI (datos del Esclavo)
// ============================================================================
static QueueHandle_t q_rx_fast = NULL;
static QueueHandle_t q_rx_slow = NULL;

// ============================================================================
// Buffer temporal para frames CEV (máximo 17 words × 2 bytes = 34 bytes)
// ============================================================================

// Ticks en los que el ensamblador no llegó a tiempo
static uint32_t s_writer_overruns = 0;

// Anillo en PSRAM. 2 MB ≈ 20 s de formato 1 (~100 KB/s). Absorbe el fsync
// de las dos SD sin frenar el ensamblado. El chunk de copia queda en SRAM.
// Anillo de bytes en PSRAM: 6 MB ≈ 60 s a ~100 KB/s (F1).
// Absorbe borrados internos y Garbage Collection de la MicroSD.
#define SD_RING_BYTES (6 * 1024 * 1024)
#define SD_IO_CHUNK   (32 * 1024)
// fsync desfasado: nunca las dos SD a la vez.
// SD1: 5, 10, 15… min  |  SD2: 7.5, 12.5, 17.5… min
// Si el anillo supera el 50%, se pospone el siguiente 60 s.
#define SD_SYNC_PERIOD_US   (5LL * 60LL * 1000000LL)
#define SD1_FIRST_SYNC_US   (5LL * 60LL * 1000000LL)
#define SD2_FIRST_SYNC_US   (7LL * 60LL * 1000000LL + 30LL * 1000000LL)
#define SD_SYNC_POSTPONE_US (60LL * 1000000LL)
static uint8_t *s_sd_ring = NULL;
static uint8_t *s_sd_chunk = NULL;
static size_t s_ring_r = 0;
static size_t s_ring_w = 0;
static size_t s_ring_used = 0;
static SemaphoreHandle_t s_ring_mtx = NULL;
static TaskHandle_t s_sd_task = NULL;
static volatile bool s_sd_stop = false;
static uint32_t s_ring_drops = 0;

// Contador de frames dentro del ciclo (0..39 para F1/F2, 0..9 para F3)
static uint8_t s_frame_counter = 0;

// Handle de la tarea escritora (para notificación de power-loss)
static TaskHandle_t s_writer_task_handle = NULL;

// Bandera para abortar escritura si la SD se llena en vuelo
static bool s_recording_aborted = false;

// ============================================================================
// ISR de Detección de Pérdida de Energía
// ============================================================================
// El comparador externo mantiene PIN_POWER_LOSS en HIGH mientras la
// tensión de 28V esté dentro de rango. Cuando cae por debajo del
// umbral (~22V), el pin baja a LOW y esta ISR se dispara.
// La ISR notifica a la tarea escritora para que cierre el archivo
// antes de que los capacitores de hold-up se agoten (~100ms).
static void IRAM_ATTR power_loss_isr(void *arg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;

  if (s_writer_task_handle != NULL) {
    // Notificación directa: el valor 1 indica power-loss
    xTaskNotifyFromISR(s_writer_task_handle, 1,
                       eSetValueWithOverwrite,
                       &xHigherPriorityTaskWoken);
  }

  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// ============================================================================
// Lectura del formato de muestreo desde NVS
// ============================================================================
static uint8_t read_format_from_nvs(void) {
  nvs_handle_t nvs;
  uint16_t format = CEV_FORMAT_1; // Default: Formato 1

  esp_err_t err = nvs_open("fdr_config", NVS_READONLY, &nvs);
  if (err == ESP_OK) {
    err = nvs_get_u16(nvs, "sample_fmt", &format);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      format = CEV_FORMAT_1;
    }
  }

  // Validar rango
  if (format < 1 || format > 3) {
    ESP_LOGW(TAG, "Formato NVS inválido (%d), usando default (1)", format);
    format = CEV_FORMAT_1;
  }

  return (uint8_t)format;
}

static bool ring_push(const uint8_t *data, size_t len) {
  if (s_sd_ring == NULL || s_ring_mtx == NULL || len == 0) {
    return false;
  }
  if (xSemaphoreTake(s_ring_mtx, pdMS_TO_TICKS(20)) != pdTRUE) {
    s_ring_drops++;
    return false;
  }
  if (s_ring_used + len > SD_RING_BYTES) {
    xSemaphoreGive(s_ring_mtx);
    s_ring_drops++;
    return false;
  }
  size_t first = SD_RING_BYTES - s_ring_w;
  if (first > len) {
    first = len;
  }
  memcpy(s_sd_ring + s_ring_w, data, first);
  if (first < len) {
    memcpy(s_sd_ring, data + first, len - first);
  }
  s_ring_w = (s_ring_w + len) % SD_RING_BYTES;
  s_ring_used += len;
  xSemaphoreGive(s_ring_mtx);
  if (s_sd_task != NULL) {
    xTaskNotifyGive(s_sd_task);
  }
  return true;
}

static size_t ring_pop(uint8_t *dst, size_t max_len) {
  if (s_sd_ring == NULL || s_ring_mtx == NULL || max_len == 0) {
    return 0;
  }
  if (xSemaphoreTake(s_ring_mtx, pdMS_TO_TICKS(50)) != pdTRUE) {
    return 0;
  }
  size_t n = s_ring_used;
  if (n > max_len) {
    n = max_len;
  }
  if (n == 0) {
    xSemaphoreGive(s_ring_mtx);
    return 0;
  }
  size_t first = SD_RING_BYTES - s_ring_r;
  if (first > n) {
    first = n;
  }
  memcpy(dst, s_sd_ring + s_ring_r, first);
  if (first < n) {
    memcpy(dst + first, s_sd_ring, n - first);
  }
  s_ring_r = (s_ring_r + n) % SD_RING_BYTES;
  s_ring_used -= n;
  xSemaphoreGive(s_ring_mtx);
  return n;
}

static size_t ring_used(void) {
  size_t n = 0;
  if (s_ring_mtx != NULL &&
      xSemaphoreTake(s_ring_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
    n = s_ring_used;
    xSemaphoreGive(s_ring_mtx);
  }
  return n;
}

// Core 0: único lugar que toca la SD. El ensamblador sigue en el core 1.
static void tsk_sd_writer(void *pvParameters) {
  (void)pvParameters;
  ESP_LOGI(TAG, "SD writer iniciado (prio 6, core 0, anillo PSRAM %u KB, fsync SD1@5m SD2@7.5m cada 5m)",
           (unsigned)(SD_RING_BYTES / 1024));
  uint8_t *chunk = s_sd_chunk;
  if (chunk == NULL) {
    ESP_LOGE(TAG, "Sin buffer de chunk de SD");
    vTaskDelete(NULL);
    return;
  }
  const int64_t t0 = esp_timer_get_time();
  int64_t next_sync1_us = t0 + SD1_FIRST_SYNC_US;
  int64_t next_sync2_us = t0 + SD2_FIRST_SYNC_US;
  int64_t last_write_us = t0;
  uint32_t bytes_out = 0;

  while (1) {
    int64_t now_us = esp_timer_get_time();
    size_t used = ring_used();
    bool sync1_due = (now_us >= next_sync1_us);
    bool sync2_due = (now_us >= next_sync2_us);
    bool timeout_flush = (used > 0 && (now_us - last_write_us >= 500000LL));

    // Escribir a SD si se acumuló al menos 1 clúster completo (32 KB = SD_IO_CHUNK),
    // o si hay que vaciar el remanente antes de un fsync, por power-loss,
    // o por timeout de inactividad (500 ms).
    if (used >= SD_IO_CHUNK || (used > 0 && (s_sd_stop || sync1_due || sync2_due || timeout_flush))) {
      size_t to_pop = (used >= SD_IO_CHUNK) ? SD_IO_CHUNK : used;
      size_t n = ring_pop(chunk, to_pop);
      if (n > 0) {
        if (!s_recording_aborted) {
          if (sd_storage_write(chunk, n) != ESP_OK) {
            ESP_LOGE(TAG, "Error crítico: Fallo de escritura en ambas SD "
                          "(posible SD llena). Abortando grabación.");
            s_recording_aborted = true;
            led_set_mode(LED_MODE_FLIGHT_ERROR);
            sd_storage_write_error_log(
                "Falla en Modo Vuelo: SD llena o error de I/O.");
            sd_storage_close();
          } else {
            bytes_out += (uint32_t)n;
            last_write_us = esp_timer_get_time();
          }
        }
      }
    } else if (!s_sd_stop && !sync1_due && !sync2_due) {
      // Esperar hasta 50 ms a que carguen más datos en el anillo
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }

    if (s_sd_stop && ring_used() == 0) {
      ESP_LOGW(TAG, "Power-loss: anillo vacío, fsync y cierre");
      sd_storage_flush_1();
      sd_storage_flush_2();
      sd_storage_close();
      ESP_LOGW(TAG, "*** Archivo cerrado exitosamente. Sistema seguro. ***");
      vTaskSuspend(NULL);
    }

    now_us = esp_timer_get_time();
    used = ring_used();
    bool ring_busy = (used > (SD_RING_BYTES / 2));

    // Sincronización periódica SD1 (cada 5 min)
    if (now_us >= next_sync1_us) {
      if (ring_busy) {
        next_sync1_us = now_us + SD_SYNC_POSTPONE_US;
        ESP_LOGW(TAG, "fsync SD1 pospuesto (anillo ocupado)");
      } else {
        sd_storage_flush_1();
        next_sync1_us = now_us + SD_SYNC_PERIOD_US;
        ESP_LOGI(TAG, "fsync SD1: %lu bytes al disco, drops_anillo=%lu",
                 (unsigned long)bytes_out, (unsigned long)s_ring_drops);
      }
    }

    // Sincronización periódica SD2 (desfasada 2.5 min respecto a SD1)
    if (now_us >= next_sync2_us) {
      if (ring_busy) {
        next_sync2_us = now_us + SD_SYNC_POSTPONE_US;
        ESP_LOGW(TAG, "fsync SD2 pospuesto (anillo ocupado)");
      } else {
        sd_storage_flush_2();
        next_sync2_us = now_us + SD_SYNC_PERIOD_US;
        ESP_LOGI(TAG, "fsync SD2: %lu bytes al disco, drops_anillo=%lu",
                 (unsigned long)bytes_out, (unsigned long)s_ring_drops);
      }
    }
  }
}

static void tsk_frame_assembler(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Ensamblado CEV Iniciada (Prio 9, core 1)");

  int frames_per_tick = sample_format_get_frames_per_tick();
  int tick_period_ms = sample_format_get_tick_period_ms();
  int words_per_frame = sample_format_get_words_per_frame();
  int frames_per_cycle = sample_format_get_frames_per_cycle();
  size_t frame_bytes = (size_t)words_per_frame * sizeof(int16_t);

  ESP_LOGI(TAG, "Formato %d: %d frames/tick, tick=%dms, %d words/frame, %d bytes/frame",
           sample_format_get_active(), frames_per_tick, tick_period_ms,
           words_per_frame, (int)frame_bytes);

  supervisor_register(TASK_ID_WRITER, "writer", 5000);

  TickType_t xLastWakeTime = xTaskGetTickCount();

  while (1) {
    supervisor_checkin(TASK_ID_WRITER);

    uint32_t power_loss = ulTaskNotifyTake(pdTRUE, 0);
    if (power_loss != 0) {
      ESP_LOGW(TAG, "*** POWER-LOSS DETECTADO! Vaciando anillo y cerrando... ***");
      s_sd_stop = true;
      if (s_sd_task != NULL) {
        xTaskNotifyGive(s_sd_task);
      }
      vTaskSuspend(NULL);
    }

    int16_t tick_frames_buf[4 * FMT1_WORDS_PER_FRAME];
    size_t tick_bytes = 0;
    for (int f = 0; f < frames_per_tick; f++) {
      int words = sample_format_assemble_frame(s_frame_counter, tick_frames_buf + (f * words_per_frame));
      if (words > 0) {
        tick_bytes += frame_bytes;
      }
      s_frame_counter++;
      if (s_frame_counter >= frames_per_cycle) {
        s_frame_counter = 0;
      }
    }

    if (tick_bytes > 0 && !s_recording_aborted) {
      while (!ring_push((const uint8_t *)tick_frames_buf, tick_bytes)) {
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    }

    // Grabación abortada: no tiene sentido seguir ensamblando frames a 1 kHz.
    // Seguimos haciendo check-in para que el supervisor no reporte una falsa
    // alarma de tarea colgada.
    if (s_recording_aborted) {
      ESP_LOGE(TAG, "Grabación detenida. La tarea de escritura queda inactiva.");
      while (1) {
        supervisor_checkin(TASK_ID_WRITER);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }

    // Detección de sobrecarga. Los frames se escriben crudos y consecutivos,
    // sin separadores ni marca de tiempo propia, así que un tick perdido deja
    // un hueco temporal que no es detectable al analizar el archivo. Contarlo
    // acá es la única forma de saber que ocurrió.
    if ((xTaskGetTickCount() - xLastWakeTime) > pdMS_TO_TICKS(tick_period_ms)) {
      s_writer_overruns++;
      if (s_writer_overruns <= 10 || s_writer_overruns % 1000 == 0) {
        ESP_LOGW(TAG, "Sobrecarga del writer #%lu (tick de %d ms vencido)",
                 (unsigned long)s_writer_overruns, tick_period_ms);
      }
    }

    // Dormir hasta el siguiente tick
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(tick_period_ms));
  }
}

// ============================================================================
// Tarea: Sincronización GPS/PPS (Prioridad 7)
// ============================================================================
static void tsk_time_sync_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Sincronización GPS Iniciada");
  supervisor_register(TASK_ID_TIME_SYNC, "time_sync", 2000);

  uint32_t log_counter = 0;
  bool rtc_calibrated = false;

  while (1) {
    supervisor_checkin(TASK_ID_TIME_SYNC);

    // Leer y parsear tramas NMEA pendientes
    gnss_process();

    // Actualizar buffer compartido con la hora actual
    gnss_time_t gtime;
    gnss_get_time(&gtime);
    if (gtime.valid) {
      // Fix GNSS activo: hora atómica satelital tiene máxima prioridad
      sample_format_update_time(gtime.day, gtime.month, gtime.year,
                                gtime.hour, gtime.minute, gtime.second);

      // Disciplinar/calibrar el RTC con el tiempo GPS la primera vez que se tiene fix
      if (!rtc_calibrated && rtc_ds3232_is_available()) {
        rtc_calibrated = true;
        rtc_ds3232_set_time(gtime.day, gtime.month, gtime.year,
                            gtime.hour, gtime.minute, gtime.second);
      }
    } else {
      // Sin fix GNSS (ej: arranque en hangar): mantener la hora actualizada desde el RTC
      if (rtc_ds3232_is_available()) {
        rtc_time_t rtime;
        if (rtc_ds3232_get_time(&rtime) == ESP_OK && rtime.valid) {
          sample_format_update_time(rtime.day, rtime.month, rtime.year,
                                    rtime.hour, rtime.minute, rtime.second);
        }
      }
    }

    // Actualizar posición GPS (Lat/Lon/Alt) para formatos CEV 2 y 3
    gnss_position_t pos;
    gnss_get_position(&pos);
    if (pos.valid) {
      sample_format_update_gnss(pos.latitude, pos.longitude, pos.altitude_m);
    }

    // Actualizar DSP 12 (señal de sincronismo = estado PPS)
    sample_format_update_dsp12(gnss_pps_is_active() ? 1 : 0);

    // Cada ~10 segundos, loguear estado del GPS
    log_counter++;
    if (log_counter >= 100) {
      log_counter = 0;

      if (gtime.valid) {
        gnss_position_t pos;
        gnss_get_position(&pos);

        ESP_LOGI(TAG, "GPS OK: %02u:%02u:%02u %02u/%02u/%04u [%.4f, %.4f] PPS=%s",
                 gtime.hour, gtime.minute, gtime.second,
                 gtime.day, gtime.month, gtime.year,
                 pos.latitude, pos.longitude,
                 gnss_pps_is_active() ? "SI" : "NO");
      } else {
        ESP_LOGW(TAG, "GPS: Sin fix satelital. PPS=%s",
                 gnss_pps_is_active() ? "SI" : "NO");
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ============================================================================
// Tarea: Procesamiento de datos rápidos recibidos del Esclavo
// ============================================================================
static void tsk_data_proc_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Procesamiento de Datos Rápidos Iniciada");

  fast_data_payload_t payload;
  supervisor_register(TASK_ID_DATA_PROC, "data_proc", 5000);

  while (1) {
    if (xQueueReceive(q_rx_fast, &payload, portMAX_DELAY) == pdTRUE) {
      supervisor_checkin(TASK_ID_DATA_PROC);

      // Copiar a buffer alineado para evitar warning de packed member
      int16_t fast_ch[16];
      memcpy(fast_ch, payload.channels, sizeof(fast_ch));
      sample_format_update_fast(fast_ch);

      // Actualizar tabla de canales para CMD 1 (canales 0..15 = rápidos)
      for (int i = 0; i < 16; i++) {
        cmd_port_update_channel(i, payload.channels[i]);
      }
    }
  }
}

// ============================================================================
// Tarea: Procesamiento de datos lentos recibidos del Esclavo
// ============================================================================
static void tsk_slow_proc_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Procesamiento de Datos Lentos Iniciada");

  slow_data_payload_t payload;
  supervisor_register(TASK_ID_SLOW_PROC, "slow_proc", 5000);

  while (1) {
    if (xQueueReceive(q_rx_slow, &payload, portMAX_DELAY) == pdTRUE) {
      supervisor_checkin(TASK_ID_SLOW_PROC);

      // Copiar a buffer alineado para evitar warning de packed member
      int16_t slow_ch[16];
      memcpy(slow_ch, payload.channels, sizeof(slow_ch));
      sample_format_update_slow(slow_ch);

      // Actualizar tabla de canales para CMD 1 (canales 16..31 = lentos)
      for (int i = 0; i < 16; i++) {
        cmd_port_update_channel(16 + i, payload.channels[i]);
      }
    }
  }
}

// ============================================================================
// Tarea: Lectura periódica de señales discretas
// ============================================================================
static void tsk_discrete_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Lectura de Discretas Iniciada");
  supervisor_register(TASK_ID_DISCRETE, "discrete", 5000);

  while (1) {
    supervisor_checkin(TASK_ID_DISCRETE);

    uint16_t current_states;
    if (discrete_signals_read(&current_states) == ESP_OK) {
      // Siempre actualizar el buffer compartido (el frame writer
      // lee el último valor cuando arma el frame con DIG)
      sample_format_update_discrete(current_states);
      for (int i = 0; i < 16; i++) {
        cmd_port_update_channel(32 + i, (int16_t)((current_states >> i) & 0x1));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz
  }
}

// ============================================================================
// Tarea: Lectura de datos IMU XSENS (Prioridad 5)
// ============================================================================
static void tsk_imu_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de IMU XSENS Iniciada");
  supervisor_register(TASK_ID_IMU, "imu", 5000);

  while (1) {
    supervisor_checkin(TASK_ID_IMU);

    if (imu_xsens_process() == ESP_OK) {
      imu_payload_t imu_data;
      if (imu_xsens_get_data(&imu_data)) {
        // Actualizar buffer compartido con DSP 1-11
        sample_format_update_imu(
            imu_data.acc_x, imu_data.acc_y, imu_data.acc_z,
            imu_data.gyr_x, imu_data.gyr_y, imu_data.gyr_z,
            imu_data.mag_x, imu_data.mag_y, imu_data.mag_z,
            imu_data.roll, imu_data.pitch, imu_data.yaw);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // 100 Hz
  }
}

// ============================================================================
// Tarea: Supervisor / Watchdog (Prioridad 3)
// ============================================================================
static void tsk_supervisor_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea Supervisor Iniciada");

  vTaskDelay(pdMS_TO_TICKS(10000));

  while (1) {
    int alarms = supervisor_check_all();
    if (alarms > 0) {
      ESP_LOGE(TAG, "*** SUPERVISOR: %d tarea(s) en alarma! ***", alarms);
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// ============================================================================
// Tarea: Puerto de Comandos RS-232 (Prioridad 4)
// ============================================================================
static void tsk_cmd_port_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Puerto de Comandos Iniciada");

  // No registrar en supervisor: esta tarea es de baja prioridad
  // y durante el vuelo no recibe datos (queda dormida sin hacer checkin).
  // Registrarla generaría falsas alarmas en vuelo.

  while (1) {
    cmd_port_process();
    // 5 ms (200 Hz): baja la latencia de respuesta del puerto de comandos.
    // cmd_port_process() es no bloqueante, así que sondear rápido es barato
    // y hace que el monitor RS-232 refresque mucho más ágil.
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

// ============================================================================
// El trigger de sincronización ahora lo maneja la primera transacción SPI (flanco de CS)
// Se ha eliminado la señal física SYNC_TRIG.

// ============================================================================
// API Pública
// ============================================================================
esp_err_t master_logic_start(void) {
  ESP_LOGI(TAG, "Arrancando Lógica de Maestro...");

  // 0. Servicio de ISR de GPIO: único punto de instalación de todo el proyecto.
  //    Lo usan la ISR de DRDY (spi_link), la de PPS (gnss) y la de power-loss.
  //    Llamarlo más de una vez funciona, pero el driver logea a nivel E en cada
  //    llamada redundante y ensucia el arranque.
  esp_err_t isr_ret = gpio_install_isr_service(0);
  if (isr_ret != ESP_OK) {
    ESP_LOGE(TAG, "No se pudo instalar el servicio de ISR de GPIO: %s",
             esp_err_to_name(isr_ret));
    return isr_ret;
  }

  // 1. Leer formato de muestreo desde NVS e inicializar ensamblador
  uint8_t format = read_format_from_nvs();
  esp_err_t ret = sample_format_init(format);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar formato de muestreo");
    return ret;
  }
  ESP_LOGI(TAG, "Formato de muestreo CEV: %d", format);

  // 2. Inicializar GNSS (UART + PPS)
  ret = gnss_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "GNSS no disponible (continuando sin GPS)");
  }

  // 3. Inicializar IMU XSENS (UART0)
  ret = imu_xsens_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "IMU XSENS no disponible (continuando sin IMU)");
  }

  // 4. Inicializar Puerto de Comandos RS-232 (UART1)
  ret = cmd_port_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Puerto de Comandos no disponible");
  }

  // 5. Inicializar lecturas discretas (I2C)
  ret = discrete_signals_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar entradas discretas");
    return ret;
  }

  // 5.1 Inicializar RTC externo DS3232 (I2C) y precargar hora base previa al fix GNSS
  ret = rtc_ds3232_init();
  if (ret == ESP_OK) {
    rtc_time_t rtime;
    if (rtc_ds3232_get_time(&rtime) == ESP_OK && rtime.valid) {
      sample_format_update_time(rtime.day, rtime.month, rtime.year,
                                rtime.hour, rtime.minute, rtime.second);
      ESP_LOGI(TAG, "Hora base inicial cargada desde RTC DS3232: %02u:%02u:%02u %02u/%02u/%04u UTC",
               rtime.hour, rtime.minute, rtime.second, rtime.day, rtime.month, rtime.year);
    }
  } else {
    ESP_LOGW(TAG, "RTC DS3232 no detectado (se utilizara hora provista por GNSS)");
  }

  // 5.5 Inicializar MicroSD ANTES de crear tareas
  //     (la negociación SPI/SDMMC necesita CPU sin interrupciones del scheduler)
  ret = sd_storage_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "MicroSD no disponible. La escritura estará deshabilitada.");
  }

  // 6. Crear Colas de recepción SPI (datos del Esclavo)
  q_rx_fast = xQueueCreate(200, sizeof(fast_data_payload_t));
  q_rx_slow = xQueueCreate(10, sizeof(slow_data_payload_t));
  if (q_rx_fast == NULL || q_rx_slow == NULL) {
    ESP_LOGE(TAG, "Error al crear colas rx_data");
    return ESP_ERR_NO_MEM;
  }

  // 7. Inicializar Enlace SPI (pasando ambas colas de recepción)
  ret = spi_link_init(q_rx_fast, q_rx_slow);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar SPI Link");
    return ret;
  }

  ret = spi_link_start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al arrancar SPI Link");
    return ret;
  }

  // 8. Archivo de vuelo y anillo en PSRAM, antes de las tareas de grabación.
  ret = sd_storage_open_flight_file(sample_format_get_active());
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "*** ERROR: No se pudo crear archivo de vuelo.");
    led_set_mode(LED_MODE_FLIGHT_ERROR);
    return ESP_FAIL;
  }
  if (!esp_psram_is_initialized()) {
    ESP_LOGE(TAG, "PSRAM no inicializada: el anillo de %u KB no cabe en SRAM",
             (unsigned)(SD_RING_BYTES / 1024));
    return ESP_ERR_NO_MEM;
  }
  s_ring_mtx = xSemaphoreCreateMutex();
  s_sd_ring = (uint8_t *)heap_caps_aligned_alloc(
      32, SD_RING_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  s_sd_chunk = (uint8_t *)heap_caps_malloc(
      SD_IO_CHUNK, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (s_ring_mtx == NULL || s_sd_ring == NULL || s_sd_chunk == NULL) {
    ESP_LOGE(TAG, "Sin RAM para anillo PSRAM (%u) o chunk de SD",
             (unsigned)SD_RING_BYTES);
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(TAG,
           "Anillo de SD: %u KB en PSRAM (PSRAM %u KB, libre %u KB; SRAM %u)",
           (unsigned)(SD_RING_BYTES / 1024),
           (unsigned)(esp_psram_get_size() / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)esp_get_free_internal_heap_size());

  BaseType_t ok = xTaskCreatePinnedToCore(tsk_sd_writer, "sd_io", 8192, NULL, 6,
                                           &s_sd_task, 0);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de SD");
    return ESP_ERR_NO_MEM;
  }
  ok = xTaskCreatePinnedToCore(tsk_frame_assembler, "assembler", 4096, NULL, 9,
                               &s_writer_task_handle, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de ensamblado");
    return ESP_ERR_NO_MEM;
  }

  // 9. Crear Tarea de Sincronización GPS (Prio 7)
  ok = xTaskCreate(tsk_time_sync_master, "time_sync", 4096, NULL, 7, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de time sync");
    return ESP_ERR_NO_MEM;
  }

  // 10. Crear Tarea de Procesamiento Rápido (Prio 5)
  ok = xTaskCreatePinnedToCore(tsk_data_proc_master, "data_proc", 4096, NULL, 5,
                               NULL, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de procesamiento rápido");
    return ESP_ERR_NO_MEM;
  }

  // 11. Crear Tarea de Procesamiento Lento (Prio 5)
  ok = xTaskCreatePinnedToCore(tsk_slow_proc_master, "slow_proc", 4096, NULL, 5,
                               NULL, 1);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de procesamiento lento");
    return ESP_ERR_NO_MEM;
  }

  // 12. Crear Tarea de Lectura de Discretas (Prio 5)
  ok = xTaskCreate(tsk_discrete_master, "discrete", 2048, NULL, 5, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de discretas");
    return ESP_ERR_NO_MEM;
  }

  // 13. Crear Tarea de IMU XSENS (Prio 5)
  ok = xTaskCreate(tsk_imu_master, "imu", 4096, NULL, 5, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de IMU");
    return ESP_ERR_NO_MEM;
  }

  // 14. Crear Tarea Puerto de Comandos (Prio 4)
  ok = xTaskCreate(tsk_cmd_port_master, "cmd_port", 4096, NULL, 4, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de comandos");
    return ESP_ERR_NO_MEM;
  }

  // 15. Crear Tarea Supervisor (Prio 3)
  ok = xTaskCreate(tsk_supervisor_master, "supervisor", 2048, NULL, 3, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea supervisor");
    return ESP_ERR_NO_MEM;
  }

  // 16. Configurar detección de pérdida de energía (Power-Loss)
  gpio_config_t pwr_conf = {
      .pin_bit_mask = (1ULL << PIN_POWER_LOSS),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,    // Pull-up: idle = HIGH
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,       // Flanco negativo = pérdida
  };
  ret = gpio_config(&pwr_conf);
  if (ret == ESP_OK) {
    gpio_isr_handler_add(PIN_POWER_LOSS, power_loss_isr, NULL);
    ESP_LOGI(TAG, "Power-Loss detector activo en GPIO %d", PIN_POWER_LOSS);
  } else {
    ESP_LOGW(TAG, "No se pudo configurar Power-Loss GPIO (continuando)");
  }

  // 17. Sincronización
  // El Esclavo despertará cuando el módulo spi_link inicie su primera transacción (CS LOW)
  ESP_LOGI(TAG, "Maestro listo. Iniciando polling SPI (despertará al Esclavo).");

  return ESP_OK;
}
