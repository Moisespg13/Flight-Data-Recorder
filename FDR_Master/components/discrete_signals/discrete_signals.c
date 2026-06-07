
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

#define DEBOUNCE_SAMPLES 3

static uint16_t s_debounced_state = 0;
static uint8_t s_debounce_counters[NUM_DISCRETE_INPUTS] = {0};

esp_err_t discrete_signals_read(uint16_t *states) {
  if (states == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  for (int i = 0; i < NUM_DISCRETE_INPUTS; i++) {
    bool is_high = gpio_get_level(disc_pins[i]);
    bool currently_debounced_high = (s_debounced_state & (1U << i)) != 0;

    if (is_high != currently_debounced_high) {
      s_debounce_counters[i]++;
      if (s_debounce_counters[i] >= DEBOUNCE_SAMPLES) {
        // Confirmado el cambio de estado (debounced)
        if (is_high) {
          s_debounced_state |= (1U << i);
        } else {
          s_debounced_state &= ~(1U << i);
        }
        s_debounce_counters[i] = 0;
      }
    } else {
      // Si el estado vuelve al valor filtrado, reseteamos el contador
      s_debounce_counters[i] = 0;
    }
  }

  *states = s_debounced_state;
  return ESP_OK;
}
