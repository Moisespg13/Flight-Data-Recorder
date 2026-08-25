/**
 * @file discrete_signals.c
 * @brief Lectura de las 16 entradas discretas usando I2C (TCA9555).
 *
 * Configura el bus I2C y se comunica con el expansor de puertos TCA9555
 * para leer el estado de las 16 entradas discretas, liberando así 16 GPIOs.
 */

#include "discrete_signals.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "pin_config.h"

static const char *TAG = "DISCRETE";

#define TCA9555_I2C_ADDR 0x20 // A0,A1,A2 a GND
#define TCA9555_CMD_IN_PORT0 0x00

esp_err_t discrete_signals_init(void) {
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = PIN_I2C_SDA,
      .scl_io_num = PIN_I2C_SCL,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = 400000, // 400 kHz Fast Mode
  };

  esp_err_t ret = i2c_param_config(I2C_NUM, &conf);
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error configurando parámetros I2C: %s", esp_err_to_name(ret));
      return ret;
  }

  ret = i2c_driver_install(I2C_NUM, conf.mode, 0, 0, 0);
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error instalando driver I2C: %s", esp_err_to_name(ret));
      return ret;
  }

  // Verificar que el chip responde
  uint8_t data[2];
  ret = i2c_master_write_read_device(I2C_NUM, TCA9555_I2C_ADDR, (uint8_t[]){TCA9555_CMD_IN_PORT0}, 1, data, 2, pdMS_TO_TICKS(100));
  if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error de comunicación con TCA9555 en 0x%02X: %s", TCA9555_I2C_ADDR, esp_err_to_name(ret));
      // No devolvemos error fatal para no clavar el vuelo si el I2C falla, pero lo logueamos
  } else {
      ESP_LOGI(TAG, "Expansor TCA9555 (I2C) configurado correctamente.");
  }

  return ESP_OK;
}

#define DEBOUNCE_SAMPLES 3
static uint16_t s_debounced_state = 0;
static uint8_t s_debounce_counters[16] = {0};

esp_err_t discrete_signals_read(uint16_t *states) {
  if (states == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t data[2];
  esp_err_t ret = i2c_master_write_read_device(I2C_NUM, TCA9555_I2C_ADDR, (uint8_t[]){TCA9555_CMD_IN_PORT0}, 1, data, 2, pdMS_TO_TICKS(10));
  
  if (ret != ESP_OK) {
      // Si falla, retornamos el último estado conocido
      *states = s_debounced_state;
      return ret;
  }

  uint16_t current_state = data[0] | (data[1] << 8);

  // Debouncing por software (opcional, el TCA9555 no tiene debounce interno)
  for (int i = 0; i < 16; i++) {
    bool is_high = (current_state & (1U << i)) != 0;
    bool currently_debounced_high = (s_debounced_state & (1U << i)) != 0;

    if (is_high != currently_debounced_high) {
      s_debounce_counters[i]++;
      if (s_debounce_counters[i] >= DEBOUNCE_SAMPLES) {
        if (is_high) {
          s_debounced_state |= (1U << i);
        } else {
          s_debounced_state &= ~(1U << i);
        }
        s_debounce_counters[i] = 0;
      }
    } else {
      s_debounce_counters[i] = 0;
    }
  }

  *states = s_debounced_state;
  return ESP_OK;
}
