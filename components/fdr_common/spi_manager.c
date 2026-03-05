
/**
 * @file spi_manager.c
 * @brief Inicialización del bus SPI según el rol (Maestro o Esclavo).
 *
 * Configura el periférico SPI y agrega los dispositivos correspondientes.
 * En modo Maestro, expone el handle del dispositivo SPI para uso externo.
 * En modo Esclavo, inicializa el driver SPI Slave para recibir del Maestro.
 */

#include "spi_manager.h"
#include "driver/spi_common.h" // IWYU pragma: keep
#include "driver/spi_master.h" // IWYU pragma: keep
#include "driver/spi_slave.h"  // IWYU pragma: keep
#include "esp_log.h"
#include "pin_config.h"
#include "sdkconfig.h"

static const char *TAG = "SPI_MANAGER";

#ifdef CONFIG_FDR_ROLE_MASTER
// Handle estático del dispositivo SPI Maestro (enlace inter-placa)
static spi_device_handle_t s_master_spi_handle = NULL;
#endif

esp_err_t spi_manager_init(void) {
  esp_err_t ret = ESP_OK;

#ifdef CONFIG_FDR_ROLE_MASTER
  ESP_LOGI(TAG, "Configurando SPI MASTER en Host ID: %d",
           CONFIG_FDR_SPI_HOST_ID);

  spi_bus_config_t buscfg = {
      .miso_io_num = PIN_SPI_MISO,
      .mosi_io_num = PIN_SPI_MOSI,
      .sclk_io_num = PIN_SPI_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .max_transfer_sz = 4096,
  };

  // Inicializamos el Bus
  ret = spi_bus_initialize(CONFIG_FDR_SPI_HOST_ID, &buscfg, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar Bus SPI Maestro: %s",
             esp_err_to_name(ret));
    return ret;
  }

  // Agregamos el Dispositivo Esclavo al Bus
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz =
          10 * 1000 * 1000, // 10 MHz (Ajustable segun calidad del cable LVDS)
      .mode = 0,            // SPI Modo 0
      .spics_io_num = PIN_SPI_CS, // Pin CS
      .queue_size = 7,            // Tamaño de la cola de transacciones
  };

  // Guardamos el handle en variable estática
  ret =
      spi_bus_add_device(CONFIG_FDR_SPI_HOST_ID, &devcfg, &s_master_spi_handle);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al agregar dispositivo Esclavo: %s",
             esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "SPI Maestro inicializado correctamente.");
  }

#elif defined(CONFIG_FDR_ROLE_SLAVE)
  ESP_LOGI(TAG, "Configurando SPI SLAVE (Enlace Inter-Placa)");

  // Configuración para el Esclavo (Link)
  spi_bus_config_t buscfg = {
      .miso_io_num = PIN_LINK_MISO,
      .mosi_io_num = PIN_LINK_MOSI,
      .sclk_io_num = PIN_LINK_CLK,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
  };

  spi_slave_interface_config_t slvcfg = {
      .mode = 0,
      .spics_io_num = PIN_LINK_CS,
      .queue_size = 3,
      .flags = 0,
  };

  // Usamos PIN_LINK_SPI_HOST (definido en pin_config.h)
  ret = spi_slave_initialize(PIN_LINK_SPI_HOST, &buscfg, &slvcfg,
                             SPI_DMA_CH_AUTO);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al inicializar SPI Esclavo: %s", esp_err_to_name(ret));
  } else {
    ESP_LOGI(TAG, "SPI Esclavo inicializado correctamente.");
  }

#else
  ESP_LOGE(TAG, "Rol no definido, no se inicializó SPI.");
  ret = ESP_ERR_INVALID_STATE;
#endif

  return ret;
}

#ifdef CONFIG_FDR_ROLE_MASTER
spi_device_handle_t spi_manager_get_master_handle(void) {
  return s_master_spi_handle;
}
#endif
