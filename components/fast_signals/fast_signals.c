
/**
 * @file fast_signals.c
 * @brief Driver para el ADC AD7606 (Señales Analógicas Rápidas).
 *
 * Implementa la inicialización, reset, disparo de conversión y
 * lectura de los 8 (por ahora) canales del AD7606 via SPI. Diseñado para
 * manejar múltiples instancias usando handles opacos.
 */

#include "fast_signals.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h" // Para esp_rom_delay_us
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "FAST_SIGNALS";

// Estructura interna del dispositivo
struct fast_signals_dev_t {
  spi_device_handle_t spi_dev;
  fast_signals_config_t config;
};

esp_err_t fast_signals_init(const fast_signals_config_t *config,
                            fast_signals_handle_t *ret_handle) {
  if (config == NULL || ret_handle == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  // Reservar memoria para el handle
  struct fast_signals_dev_t *dev = calloc(1, sizeof(struct fast_signals_dev_t));
  if (dev == NULL) {
    return ESP_ERR_NO_MEM;
  }
  dev->config = *config;

  // Configurar GPIOs de Control (Salidas)
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << config->pin_cs) | (1ULL << config->pin_convst) |
                      (1ULL << config->pin_rst),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);

  // Configurar Busy como entrada
  gpio_config_t busy_conf = {
      .pin_bit_mask = (1ULL << config->pin_busy),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE, // Podría ser interrupción en el futuro
  };
  gpio_config(&busy_conf);

  // Estado inicial
  gpio_set_level(config->pin_cs, 1);
  gpio_set_level(config->pin_convst, 1);
  gpio_set_level(config->pin_rst, 0);

  // Configurar Dispositivo SPI
  spi_device_interface_config_t devcfg = {
      .command_bits = 0,
      .address_bits = 0,
      .dummy_bits = 0,
      .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
      .duty_cycle_pos = 128,
      .mode = 2, // AD7606 suele trabajar en Modo 2 (CPOL=1, CPHA=0)
      .spics_io_num = config->pin_cs,
      .queue_size = 7,
  };

  esp_err_t ret = spi_bus_add_device(config->spi_host, &devcfg, &dev->spi_dev);
  if (ret != ESP_OK) {
    free(dev);
    return ret;
  }

  *ret_handle = dev;
  ESP_LOGI(TAG, "Dispositivo de Señales Rápidas inicializado (CS=%d)",
           config->pin_cs);
  return ESP_OK;
}

esp_err_t fast_signals_reset(fast_signals_handle_t handle) {
  if (handle == NULL)
    return ESP_ERR_INVALID_ARG;

  gpio_set_level(handle->config.pin_rst, 1);
  esp_rom_delay_us(2); // Pulso min 50ns
  gpio_set_level(handle->config.pin_rst, 0);

  return ESP_OK;
}

esp_err_t fast_signals_start_conversion(fast_signals_handle_t handle) {
  if (handle == NULL)
    return ESP_ERR_INVALID_ARG;

  // Pulso negativo en CONVST para iniciar
  gpio_set_level(handle->config.pin_convst, 0);
  esp_rom_delay_us(1);
  gpio_set_level(handle->config.pin_convst, 1);

  return ESP_OK;
}

esp_err_t fast_signals_read(fast_signals_handle_t handle,
                            int16_t *data_buffer) {
  if (handle == NULL || data_buffer == NULL)
    return ESP_ERR_INVALID_ARG;

  // Esperar a que BUSY baje (conversión lista)
  int timeout = 1000;
  while (gpio_get_level(handle->config.pin_busy) == 1 && timeout > 0) {
    esp_rom_delay_us(1);
    timeout--;
  }
  if (timeout == 0) {
    ESP_LOGW(TAG, "Timeout esperando BUSY");
    return ESP_ERR_TIMEOUT;
  }

  // Leer datos SPI (8 canales * 2 bytes = 16 bytes)
  spi_transaction_t t;
  memset(&t, 0, sizeof(t));
  t.length = 8 * 16; // 128 bits
  t.rx_buffer = data_buffer;

  return spi_device_transmit(handle->spi_dev, &t);
}
