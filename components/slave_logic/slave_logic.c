
/**
 * @file slave_logic.c
 * @brief Lógica principal del Esclavo (Placa de Adquisición).
 *
 * Inicializa el hardware de adquisición (AD7606), crea las colas
 * de datos y espera la señal SYNC_TRIG del Maestro antes de
 * lanzar las tareas de captura y el enlace SPI.
 */

#include "slave_logic.h"
#include "driver/gpio.h"       // IWYU pragma: keep
#include "esp_log.h"           // IWYU pragma: keep
#include "esp_timer.h"         // IWYU pragma: keep
#include "fast_signals.h"      // IWYU pragma: keep
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"    // IWYU pragma: keep
#include "freertos/task.h"     // IWYU pragma: keep
#include "pin_config.h"        // IWYU pragma: keep
#include "protocol.h"          // IWYU pragma: keep
#include "sdkconfig.h"         // IWYU pragma: keep
#include "spi_link.h"          // IWYU pragma: keep
#include <string.h>            // IWYU pragma: keep


#ifdef CONFIG_FDR_ROLE_SLAVE

static const char *TAG = "SLAVE_LOGIC";
static fast_signals_handle_t adc1_handle = NULL;

// Cola para enviar datos rápidos al SPI Link
static QueueHandle_t q_fast_data = NULL;

// Handle de la tarea coordinadora (recibe notificación del ISR)
static TaskHandle_t s_coord_task_handle = NULL;

// ISR: Se ejecuta cuando PIN_SYNC_TRIG sube (flanco ascendente)
static void IRAM_ATTR sync_trig_isr(void *arg) {
  BaseType_t higher_prio_woken = pdFALSE;
  // Notificar a la tarea coordinadora
  vTaskNotifyGiveFromISR(s_coord_task_handle, &higher_prio_woken);
  portYIELD_FROM_ISR(higher_prio_woken);
}

// Tarea de adquisición rápida
static void tsk_acq_fast(void *pvParameters) {
  ESP_LOGI(TAG, "Tarea de Adquisición Rápida Iniciada");

  fast_data_payload_t payload;

  while (1) {
    // Inicializar payload a 0 (canales 9-16 no disponibles aún)
    memset(&payload, 0, sizeof(payload));

    if (adc1_handle) {
      fast_signals_start_conversion(adc1_handle);

      // Leemos los primeros 8 canales
      esp_err_t ret = fast_signals_read(adc1_handle, payload.channels);

      if (ret == ESP_OK) {
        // Capturar tiempo exacto post-conversión (convertir a ns)
        payload.timestamp_ns = (uint64_t)esp_timer_get_time() * 1000;

        if (q_fast_data) {
          // Timeout 0: Si la cola está llena, descartamos (Drop)
          xQueueSend(q_fast_data, &payload, 0);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // ~100Hz por ahora
  }
}

// Tarea coordinadora: espera SYNC_TRIG y luego arranca todo
static void tsk_coordinator(void *pvParameters) {
  ESP_LOGI(TAG, "Esperando SYNC_TRIG del Maestro (GPIO %d)...", PIN_SYNC_TRIG);

  // Bloquear hasta recibir la notificación del ISR
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  ESP_LOGI(TAG, "*** SYNC recibido! Arrancando adquisición. ***");

  // Arrancar Enlace SPI
  spi_link_start();

  // Crear Tarea de Adquisición (Prio 7 = Alta)
  xTaskCreate(tsk_acq_fast, "tsk_acq_fast", 4096, NULL, 7, NULL);

  ESP_LOGI(TAG, "Tareas de adquisición en marcha.");

  // Esta tarea ya cumplió su propósito, se autodestruye
  vTaskDelete(NULL);
}

esp_err_t slave_logic_start(void) {
  ESP_LOGI(TAG, "Arrancando Lógica de Esclavo...");

  // 0. Crear Cola de datos rápidos
  // ---------------------------------------------------------
  q_fast_data = xQueueCreate(50, sizeof(fast_data_payload_t));
  if (q_fast_data == NULL) {
    ESP_LOGE(TAG, "Error al crear cola fast_data");
    return ESP_ERR_NO_MEM;
  }

  // 1. Inicializar Enlace SPI (solo init, NO start todavía)
  // ---------------------------------------------------------
  spi_link_init(q_fast_data);

  // 2. Inicializar Hardware de Adquisición (AD7606)
  // ---------------------------------------------------------
  fast_signals_config_t cfg_ad7606_1 = {.spi_host = PIN_ACQ_SPI_HOST,
                                        .pin_cs = PIN_CS_AD7606_1,
                                        .pin_convst = PIN_ADC_CNV,
                                        .pin_busy = PIN_ADC_BUSY,
                                        .pin_rst = PIN_ADC_RST};

  esp_err_t ret = fast_signals_init(&cfg_ad7606_1, &adc1_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Fallo critico al iniciar AD7606 #1");
    return ret;
  }

  fast_signals_reset(adc1_handle);
  ESP_LOGI(TAG, "AD7606 #1 listo.");

  // 3. Crear Tarea Coordinadora (espera SYNC_TRIG)
  // ---------------------------------------------------------
  // Necesitamos el handle ANTES de registrar el ISR
  BaseType_t ok = xTaskCreate(tsk_coordinator, "tsk_coord", 2048, NULL, 8,
                              &s_coord_task_handle);
  if (ok != pdPASS) {
    ESP_LOGE(TAG, "Error al crear tarea coordinadora");
    return ESP_ERR_NO_MEM;
  }

  // 4. Configurar GPIO SYNC_TRIG como entrada con interrupción
  // ---------------------------------------------------------
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << PIN_SYNC_TRIG),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE, // Pull-down: LOW por defecto
      .intr_type = GPIO_INTR_POSEDGE,       // Flanco ascendente
  };
  ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar SYNC_TRIG");
    return ret;
  }

  // Instalar servicio de ISR GPIO (si no está instalado aún)
  gpio_install_isr_service(0);
  gpio_isr_handler_add(PIN_SYNC_TRIG, sync_trig_isr, NULL);

  ESP_LOGI(TAG, "Esclavo inicializado. Aguardando señal del Maestro...");
  return ESP_OK;
}

#else // CONFIG_FDR_ROLE_MASTER

esp_err_t slave_logic_start(void) { return ESP_ERR_NOT_SUPPORTED; }

#endif
