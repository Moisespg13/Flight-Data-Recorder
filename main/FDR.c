/**
 * @file FDR.c
 * @brief Punto de entrada principal del firmware FDR (Flight Data Recorder).
 *
 * Inicializa los subsistemas comunes (NVS, SPI) y delega la lógica
 * específica al componente correspondiente según el rol configurado
 * en Kconfig: master_logic (Maestro) o slave_logic (Esclavo).
 */

#include "esp_log.h"           // IWYU pragma: keep
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/task.h"     // IWYU pragma: keep
#include "nvs.h"               // IWYU pragma: keep
#include "nvs_flash.h"         // IWYU pragma: keep
#include "sdkconfig.h"         // IWYU pragma: keep

#include "fast_signals.h" // IWYU pragma: keep
#include "master_logic.h" // IWYU pragma: keep
#include "pin_config.h"   // IWYU pragma: keep
#include "protocol.h"     // IWYU pragma: keep
#include "slave_logic.h"  // IWYU pragma: keep
#include "spi_manager.h"  // IWYU pragma: keep

static const char *TAG = "FDR_MAIN";

void app_main(void) {
  // Inicializar NVS (Requerido para WiFi y PHY, aunque no usemos WiFi por ahora
  // es buena practica)
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "Arrancando FDR Firmware...");

  // Inicializar SPI Manager (Maestro o Esclavo según Kconfig)
  if (spi_manager_init() != ESP_OK) {
    ESP_LOGE(TAG, "Fallo al inicializar SPI Manager");
  } else {
    ESP_LOGI(TAG, "SPI Manager inicializado.");
  }

#ifdef CONFIG_FDR_ROLE_MASTER
  ESP_LOGI(TAG, "Rol: MAESTRO (Almacenamiento & Comunicación)");
  ESP_LOGI(TAG, "SPI Host ID: %d", CONFIG_FDR_SPI_HOST_ID);
  ESP_LOGI(TAG, "Pines: MISO=%d, MOSI=%d, CLK=%d, CS=%d", PIN_SPI_MISO,
           PIN_SPI_MOSI, PIN_SPI_CLK, PIN_SPI_CS);

  // Inicializa enlace SPI y lanza tarea de procesamiento de datos
  master_logic_start();
#elif defined(CONFIG_FDR_ROLE_SLAVE)
  // --- Lógica del Esclavo ---
  // Inicializa hardware de adquisición y arranca tareas (tsk_acq_fast, etc.)
  slave_logic_start();

#else
  ESP_LOGE(TAG, "Rol NO definido! Revisa Kconfig.");
#endif

  ESP_LOGI(TAG, "Protocol Magic: 0x%02X", FDR_PROTO_MAGIC);

  while (1) {
    // El bucle principal del Main se queda idle.
    // Las tareas de FreeRTOS (creadas en slave_logic_start) hacen el trabajo.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
