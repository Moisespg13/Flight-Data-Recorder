
/**
 * @file spi_manager.c
 * @brief Inicialización del bus SPI Maestro (enlace inter-placa).
 *
 * Configura el periférico SPI como Maestro y agrega el dispositivo
 * Esclavo (RP2040) al bus.
 */

#include "spi_manager.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "pin_config.h"

static const char *TAG = "SPI_MANAGER";

static spi_device_handle_t s_master_spi_handle = NULL;

esp_err_t spi_manager_init(void) {
  ESP_LOGI(TAG, "Configurando SPI MASTER (enlace inter-placa)");

  spi_bus_config_t buscfg = {
      .miso_io_num = PIN_SPI_MISO,
      .mosi_io_num = PIN_SPI_MOSI,
      .sclk_io_num = PIN_SPI_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4096,
  };

  esp_err_t ret =
      spi_bus_initialize(PIN_LINK_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar Bus SPI: %s", esp_err_to_name(ret));
    return ret;
  }

  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
      .mode = 0,
      .spics_io_num = PIN_SPI_CS,
      .queue_size = 7,
  };

  ret = spi_bus_add_device(PIN_LINK_SPI_HOST, &devcfg, &s_master_spi_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al agregar dispositivo Esclavo: %s",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "SPI Maestro inicializado correctamente.");
  }

  return ret;
}

spi_device_handle_t spi_manager_get_master_handle(void) {
  return s_master_spi_handle;
}
