
/**
 * @file fast_signals.c
 * @brief Driver para los ADC AD7606 (Señales Rápidas) - Pico SDK.
 *
 * Implementa la inicialización, reset, disparo de conversión y
 * lectura de los 16 canales (8 por chip) AD7606 vía SPI0.
 *
 * Ambos AD7606 comparten:
 *   - Bus SPI0 (SCK, MISO, MOSI)
 *   - Pin CONVST (conversión simultánea)
 *   - Pin RESET (reset conjunto)
 *
 * Cada AD7606 tiene su propio:
 *   - Chip Select (CS)
 *   - Pin BUSY
 */

#include "fast_signals.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "pin_config.h"
#include <string.h>

// --- Tablas de pines por chip ---
// Índice 0 = AD7606 #1, Índice 1 = AD7606 #2
static const uint8_t cs_pins[2] = {PIN_CS_AD7606_1, PIN_CS_AD7606_2};
static const uint8_t busy_pins[2] = {PIN_ADC_BUSY, PIN_ADC2_BUSY};

void fast_signals_init(void) {
  // Inicializar SPI0 a 10 MHz
  spi_init(PIN_ACQ_SPI_INST, 10 * 1000 * 1000);

  // Configurar modo SPI: CPOL=1, CPHA=0 (Modo 2 para AD7606)
  spi_set_format(PIN_ACQ_SPI_INST, 16, SPI_CPOL_1, SPI_CPHA_0, SPI_MSB_FIRST);

  // Asignar funciones SPI a los pines del bus
  gpio_set_function(PIN_ACQ_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_ACQ_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_ACQ_MOSI, GPIO_FUNC_SPI);

  // Inicializar CS y BUSY de cada chip
  for (int i = 0; i < FAST_SIGNALS_NUM_CHIPS; i++) {
    // CS como GPIO manual (salida, inactivo = alto)
    gpio_init(cs_pins[i]);
    gpio_set_dir(cs_pins[i], GPIO_OUT);
    gpio_put(cs_pins[i], 1);

    // BUSY como entrada
    gpio_init(busy_pins[i]);
    gpio_set_dir(busy_pins[i], GPIO_IN);
  }

  // CONVST compartido (salida, idle = HIGH)
  gpio_init(PIN_ADC_CONVST);
  gpio_set_dir(PIN_ADC_CONVST, GPIO_OUT);
  gpio_put(PIN_ADC_CONVST, 1);

  // RESET compartido (salida, idle = LOW)
  gpio_init(PIN_ADC_RST);
  gpio_set_dir(PIN_ADC_RST, GPIO_OUT);
  gpio_put(PIN_ADC_RST, 0);

  // Reset inicial de ambos ADCs
  fast_signals_reset();
}

void fast_signals_reset(void) {
  gpio_put(PIN_ADC_RST, 1);
  busy_wait_us(2); // Pulso mínimo 50ns, usamos 2us por seguridad
  gpio_put(PIN_ADC_RST, 0);
}

void fast_signals_start_conversion(void) {
  // Pulso negativo en CONVST (compartido) -> ambos ADCs convierten
  // simultáneamente
  gpio_put(PIN_ADC_CONVST, 0);
  busy_wait_us(1);
  gpio_put(PIN_ADC_CONVST, 1);
}

/**
 * @brief Espera a que el pin BUSY de un chip baje (conversión lista).
 * @param chip_index Índice del chip (0 o 1)
 * @return 0 si OK, -1 si timeout
 */
static int wait_busy(int chip_index) {
  int timeout = 1000; // ~1 ms máximo
  while (gpio_get(busy_pins[chip_index]) == 1 && timeout > 0) {
    busy_wait_us(1);
    timeout--;
  }
  return (timeout == 0) ? -1 : 0;
}

/**
 * @brief Lee los 8 canales de un chip AD7606 específico.
 * @param chip_index Índice del chip (0 o 1)
 * @param data_buffer Buffer de al menos 8 int16_t
 * @return 0 si OK, -1 si timeout
 */
static int read_single_chip(int chip_index, int16_t *data_buffer) {
  // Esperar a que BUSY baje
  if (wait_busy(chip_index) != 0) {
    return -1;
  }

  // Leer 8 canales * 16 bits
  gpio_put(cs_pins[chip_index], 0); // CS activo (bajo)
  spi_read16_blocking(PIN_ACQ_SPI_INST, 0, (uint16_t *)data_buffer,
                      FAST_SIGNALS_CH_PER_CHIP);
  gpio_put(cs_pins[chip_index], 1); // CS inactivo (alto)

  return 0;
}

int fast_signals_read(int16_t *data_buffer) {
  if (data_buffer == NULL) {
    return -1;
  }

  // Limpiar el buffer completo (16 canales)
  memset(data_buffer, 0, FAST_SIGNALS_TOTAL_CH * sizeof(int16_t));

  // Leer AD7606 #1 -> canales [0..7]
  if (read_single_chip(0, &data_buffer[0]) != 0) {
    return -1;
  }

  // Leer AD7606 #2 -> canales [8..15]
  // (Si solo hay 1 chip, este bloque no se ejecuta y los canales quedan en 0)
#if FAST_SIGNALS_NUM_CHIPS >= 2
  if (read_single_chip(1, &data_buffer[FAST_SIGNALS_CH_PER_CHIP]) != 0) {
    return -1;
  }
#endif

  return 0;
}
