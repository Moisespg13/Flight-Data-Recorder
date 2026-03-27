
/**
 * @file discrete_signals.c
 * @brief Lectura de las 16 entradas discretas directas (GPIO).
 *
 * Configura los GPIO como entradas con pull-down interno y
 * provee una función para leer todos los estados en un uint16_t.
 * Reemplaza los MAX22190 que fueron descartados.
 */

#include "discrete_signals.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "pin_config.h"

static const char *TAG = "DISCRETE";

// Array estático con los números de GPIO de cada entrada discreta
static const int disc_pins[NUM_DISCRETE_INPUTS] = PIN_DISC_LIST;

esp_err_t discrete_signals_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = PIN_DISC_MASK,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };

  esp_err_t ret = gpio_config(&io_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar GPIOs discretos: %s",
             esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "%d entradas discretas configuradas.", NUM_DISCRETE_INPUTS);
  return ESP_OK;
}

esp_err_t discrete_signals_read(uint16_t *states) {
  if (states == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint16_t result = 0;
  for (int i = 0; i < NUM_DISCRETE_INPUTS; i++) {
    if (gpio_get_level(disc_pins[i])) {
      result |= (1U << i);
    }
  }

  *states = result;
  return ESP_OK;
}
