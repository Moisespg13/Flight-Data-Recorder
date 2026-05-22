
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
 *   Prio 8 - tsk_frame_writer_master: Ensambla frames CEV y los graba en SD
 *   Prio 7 - tsk_time_sync_master:    Sincronización GPS/PPS
 *   Prio 6 - tsk_spi_link_master:     Recibe datos del Esclavo por SPI
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
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gnss.h"
#include "imu_xsens.h"
#include "nvs.h"
#include "pin_config.h"
#include "protocol.h"
#include "sample_format.h"
#include "sd_storage.h"
#include "supervisor.h"
#include "spi_link.h"

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
static int16_t s_frame_buf[FMT1_WORDS_PER_FRAME]; // 17 = máximo posible

// Contador de frames dentro del ciclo (0..39 para F1/F2, 0..9 para F3)
static uint8_t s_frame_counter = 0;

// Handle de la tarea escritora (para notificación de power-loss)
static TaskHandle_t s_writer_task_handle = NULL;

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

// ============================================================================
// Tarea: Escritor de Frames CEV a MicroSD (Prioridad 8 - MÁXIMA)
// ============================================================================
/**
 * @brief Ensambla frames CEV a tasa fija y los graba en la MicroSD.
 *
 * La tasa depende del formato activo:
 *   F1/F2: cada 1 ms ensambla 3 frames (= 3000 frames/seg)
 *   F3:    cada 10 ms ensambla 1 frame (= 100 frames/seg)
 *
 * Cada frame es un bloque de N words de 16 bits (N = 17, 11 o 6),
 * escritos consecutivamente sin separadores ni headers.
 */
static void tsk_frame_writer_master(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Escritura CEV Iniciada");

  // La MicroSD ya fue inicializada en master_logic_start().
  // Abrir el archivo de vuelo con el formato CEV activo.
  esp_err_t ret = sd_storage_open_flight_file(sample_format_get_active());
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "*** ERROR: No se pudo crear archivo de vuelo.");
    vTaskDelete(NULL);
    return;
  }

  // 3. Configuración del formato
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

    // ── Verificar señal de Power-Loss ──────────────────────────────
    // Si la ISR envió una notificación, el valor será != 0.
    // Usamos ulTaskNotifyTake para revisar sin bloquear (timeout = 0).
    uint32_t power_loss = ulTaskNotifyTake(pdTRUE, 0);
    if (power_loss != 0) {
      ESP_LOGW(TAG, "*** POWER-LOSS DETECTADO! Cerrando archivo de vuelo... ***");

      // Flush de emergencia: volcar buffer + fsync + fclose
      sd_storage_flush();
      sd_storage_close();

      ESP_LOGW(TAG, "*** Archivo cerrado exitosamente. Sistema seguro. ***");

      // Suspender esta tarea para siempre (ya no hay nada que escribir)
      vTaskSuspend(NULL);
    }

    // ── Ensamblar N frames por tick ────────────────────────────────
    for (int f = 0; f < frames_per_tick; f++) {
      int words = sample_format_assemble_frame(s_frame_counter, s_frame_buf);

      if (words > 0) {
        ret = sd_storage_write(s_frame_buf, (size_t)words * sizeof(int16_t));
        if (ret != ESP_OK) {
          ESP_LOGE(TAG, "Error al escribir frame %d en SD!", s_frame_counter);
        }
      }

      // Avanzar contador circular
      s_frame_counter++;
      if (s_frame_counter >= frames_per_cycle) {
        s_frame_counter = 0;
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

  while (1) {
    supervisor_checkin(TASK_ID_TIME_SYNC);

    // Leer y parsear tramas NMEA pendientes
    gnss_process();

    // Actualizar buffer compartido con la hora actual
    gnss_time_t gtime;
    gnss_get_time(&gtime);
    if (gtime.valid) {
      sample_format_update_time(gtime.day, gtime.month, gtime.year,
                                gtime.hour, gtime.minute, gtime.second);
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
            imu_data.roll, imu_data.pitch);
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
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ============================================================================
// Configuración de la señal SYNC_TRIG
// ============================================================================
static esp_err_t sync_trigger_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << PIN_SYNC_TRIG),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    return ret;
  }
  gpio_set_level(PIN_SYNC_TRIG, 0);
  return ESP_OK;
}

// ============================================================================
// API Pública
// ============================================================================
esp_err_t master_logic_start(void) {
  ESP_LOGI(TAG, "Arrancando Lógica de Maestro...");

  // 0. Configurar señal de sincronización
  esp_err_t ret = sync_trigger_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar SYNC_TRIG");
    return ret;
  }
  ESP_LOGI(TAG, "SYNC_TRIG (GPIO %d) configurado como salida.", PIN_SYNC_TRIG);

  // 1. Leer formato de muestreo desde NVS e inicializar ensamblador
  uint8_t format = read_format_from_nvs();
  ret = sample_format_init(format);
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

  // 5. Inicializar lecturas discretas
  ret = discrete_signals_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar entradas discretas");
    return ret;
  }

  // 5.5 Inicializar MicroSD ANTES de crear tareas
  //     (la negociación SPI/SDMMC necesita CPU sin interrupciones del scheduler)
  ret = sd_storage_init();
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "MicroSD no disponible. La escritura estará deshabilitada.");
  }

  // 6. Crear Colas de recepción SPI (datos del Esclavo)
  q_rx_fast = xQueueCreate(50, sizeof(fast_data_payload_t));
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

  // 8. Crear Tarea de Escritura CEV a SD (Prio 8 - MÁXIMA)
  BaseType_t ok =
      xTaskCreate(tsk_frame_writer_master, "writer", 8192, NULL, 8,
                  &s_writer_task_handle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de escritura");
    return ESP_ERR_NO_MEM;
  }

  // 9. Crear Tarea de Sincronización GPS (Prio 7)
  ok = xTaskCreate(tsk_time_sync_master, "time_sync", 4096, NULL, 7, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de time sync");
    return ESP_ERR_NO_MEM;
  }

  // 10. Crear Tarea de Procesamiento Rápido (Prio 5)
  ok = xTaskCreate(tsk_data_proc_master, "data_proc", 4096, NULL, 5, NULL);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea de procesamiento rápido");
    return ESP_ERR_NO_MEM;
  }

  // 11. Crear Tarea de Procesamiento Lento (Prio 5)
  ok = xTaskCreate(tsk_slow_proc_master, "slow_proc", 4096, NULL, 5, NULL);
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
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_POWER_LOSS, power_loss_isr, NULL);
    ESP_LOGI(TAG, "Power-Loss detector activo en GPIO %d", PIN_POWER_LOSS);
  } else {
    ESP_LOGW(TAG, "No se pudo configurar Power-Loss GPIO (continuando)");
  }

  // 17. Disparar sincronización: Esclavo puede arrancar
  ESP_LOGI(TAG, "Maestro listo. Enviando SYNC_TRIG al Esclavo (RP2040)...");
  gpio_set_level(PIN_SYNC_TRIG, 1);

  return ESP_OK;
}
