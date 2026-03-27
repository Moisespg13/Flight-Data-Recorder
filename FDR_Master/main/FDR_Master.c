/**
 * @file FDR_Master.c
 * @brief Punto de entrada principal del firmware Maestro (ESP32-S3).
 *
 * Inicializa NVS, SPI de enlace inter-placa, lectura de señales
 * discretas por GPIO, y lanza las tareas de recepción de datos
 * analógicos desde el Esclavo (RP2040) y almacenamiento.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "master_logic.h"
#include "pin_config.h"
#include "protocol.h"

static const char *TAG = "FDR_MASTER";

void app_main(void) {
  // Inicializar NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "=== FDR MAESTRO (ESP32-S3) ===");
  ESP_LOGI(TAG, "Pines SPI Link: MISO=%d, MOSI=%d, CLK=%d, CS=%d",
           PIN_SPI_MISO, PIN_SPI_MOSI, PIN_SPI_CLK, PIN_SPI_CS);
  ESP_LOGI(TAG, "Protocol Magic: 0x%02X", FDR_PROTO_MAGIC);

  // Arrancar toda la lógica del Maestro
  ret = master_logic_start();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error fatal al iniciar lógica del Maestro!");
  }

  // El bucle principal queda idle.
  // Las tareas de FreeRTOS hacen todo el trabajo.
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
