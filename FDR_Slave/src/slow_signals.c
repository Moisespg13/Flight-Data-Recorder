
/**
 * @file slow_signals.c
 * @brief Driver para el ADC AD4131-8 (Señales Lentas) - Pico SDK.
 *
 * Implementa la inicialización y lectura de 16 canales single-ended
 * del AD4131-8 vía SPI0 (compartido con los AD7606).
 *
 * El acceso al bus SPI0 se coordina con fast_signals mediante un
 * mutex de FreeRTOS. Antes de cada transacción, se reconfigura
 * el bus al Modo 3 (CPOL=1, CPHA=1) requerido por el AD4131,
 * y al finalizar se restaura al Modo 2 del AD7606.
 *
 * Protocolo SPI del AD4131:
 *   - Registros de 24 bits: 8-bit comando + 16-bit dato
 *   - Lectura de datos: CS bajo → enviar comando de lectura →
 *     recibir 24 bits de datos por canal → CS alto
 *   - DRDY (Data Ready) se pone bajo cuando hay datos listos
 */

#include "slow_signals.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "pin_config.h"
#include <string.h>
#include <stdio.h>

// Mutex compartido para SPI0
static SemaphoreHandle_t s_spi0_mutex = NULL;

// --- Registros del AD4131 (simplificados) ---
// Estos son representativos; ajustar según la hoja de datos exacta.
#define AD4131_REG_STATUS     0x00
#define AD4131_REG_DATA       0x04
#define AD4131_REG_CHANNEL(n) (0x09 + (n))  // Registros de canal 0-15
#define AD4131_REG_SETUP(n)   (0x19 + (n))  // Configuración de setup
#define AD4131_REG_ADC_CTRL   0x01
#define AD4131_REG_IO_CTRL    0x03

// Comando SPI: bit 6 = R/W (1=read), bits 5:0 = dirección de registro
#define AD4131_CMD_READ(reg)  (0x40 | ((reg) & 0x3F))
#define AD4131_CMD_WRITE(reg) (0x00 | ((reg) & 0x3F))

/**
 * @brief Reconfigura SPI0 al Modo 3 (AD4131) y toma el mutex.
 * @return true si se pudo tomar el mutex, false si timeout.
 */
static bool spi0_acquire_for_slow(void) {
  if (xSemaphoreTake(s_spi0_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  // Cambiar a Modo 3 (CPOL=1, CPHA=1) para AD4131
  spi_set_format(PIN_ACQ_SPI_INST, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
  return true;
}

/**
 * @brief Restaura SPI0 al Modo 2 (AD7606) y libera el mutex.
 */
static void spi0_release_for_slow(void) {
  // Restaurar Modo 2 (CPOL=1, CPHA=0) para AD7606
  spi_set_format(PIN_ACQ_SPI_INST, 16, SPI_CPOL_1, SPI_CPHA_0, SPI_MSB_FIRST);
  xSemaphoreGive(s_spi0_mutex);
}

/**
 * @brief Escribe un registro del AD4131.
 * @note Requiere que el mutex ya esté tomado.
 */
static void ad4131_write_reg(uint8_t reg, uint16_t value) {
  uint8_t tx[3];
  tx[0] = AD4131_CMD_WRITE(reg);
  tx[1] = (uint8_t)(value >> 8);
  tx[2] = (uint8_t)(value & 0xFF);

  gpio_put(PIN_CS_AD4131, 0);
  spi_write_blocking(PIN_ACQ_SPI_INST, tx, 3);
  gpio_put(PIN_CS_AD4131, 1);
  busy_wait_us(1); // Mínimo CS high time
}

/**
 * @brief Lee un registro del AD4131 (16 bits).
 * @note Requiere que el mutex ya esté tomado.
 */
static uint16_t ad4131_read_reg(uint8_t reg) {
  uint8_t tx_cmd = AD4131_CMD_READ(reg);
  uint8_t rx[2] = {0};

  gpio_put(PIN_CS_AD4131, 0);
  spi_write_blocking(PIN_ACQ_SPI_INST, &tx_cmd, 1);
  spi_read_blocking(PIN_ACQ_SPI_INST, 0x00, rx, 2);
  gpio_put(PIN_CS_AD4131, 1);

  return ((uint16_t)rx[0] << 8) | rx[1];
}

/**
 * @brief Espera a que DRDY baje (dato listo).
 * @return 0 si OK, -1 si timeout.
 */
static int wait_drdy(void) {
  int timeout = 5000; // ~5 ms (a 1 us por iteración)
  while (gpio_get(PIN_AD4131_DRDY) == 1 && timeout > 0) {
    busy_wait_us(1);
    timeout--;
  }
  return (timeout == 0) ? -1 : 0;
}

void slow_signals_init(SemaphoreHandle_t spi0_mutex) {
  s_spi0_mutex = spi0_mutex;

  // Configurar CS del AD4131 como GPIO (salida, inactivo = alto)
  gpio_init(PIN_CS_AD4131);
  gpio_set_dir(PIN_CS_AD4131, GPIO_OUT);
  gpio_put(PIN_CS_AD4131, 1);

  // Configurar DRDY como entrada (activo bajo)
  gpio_init(PIN_AD4131_DRDY);
  gpio_set_dir(PIN_AD4131_DRDY, GPIO_IN);
  gpio_pull_up(PIN_AD4131_DRDY); // Pull-up: DRDY idles high

  // Configurar registros del AD4131 (requiere acceso SPI)
  if (spi0_acquire_for_slow()) {
    // Habilitar canales 0-15 en modo escaneo continuo
    for (int ch = 0; ch < SLOW_SIGNALS_NUM_CHANNELS; ch++) {
      // Cada registro de canal: habilitar canal, asignar setup 0
      // Bit 15 = enable, bits [14:12] = setup_sel, [9:5] = ainp, [4:0] = ainm
      uint16_t ch_config = (1 << 15) | (ch << 5); // enable + input mux
      ad4131_write_reg(AD4131_REG_CHANNEL(ch), ch_config);
    }

    // Configurar modo de conversión continua
    // ADC_CTRL: modo continuo, referencia interna
    ad4131_write_reg(AD4131_REG_ADC_CTRL, 0x0000);

    spi0_release_for_slow();

    printf("[SLOW] AD4131 inicializado (%d canales, modo continuo).\n",
           SLOW_SIGNALS_NUM_CHANNELS);
  } else {
    printf("[SLOW] *** ERROR: No se pudo tomar el mutex SPI0! ***\n");
  }
}

int slow_signals_read(int16_t *data_buffer) {
  if (data_buffer == NULL) {
    return -1;
  }

  // Limpiar buffer
  memset(data_buffer, 0, SLOW_SIGNALS_NUM_CHANNELS * sizeof(int16_t));

  // Leer los 16 canales secuencialmente
  for (int ch = 0; ch < SLOW_SIGNALS_NUM_CHANNELS; ch++) {
    // 1. Esperar a que el ADC tenga un dato listo SIN TENER EL MUTEX.
    // Esto es vital para no bloquear al AD7606 (3000 Hz) mientras esperamos.
    if (wait_drdy() != 0) {
      printf("[SLOW] Timeout en DRDY (canal %d)\n", ch);
      return -1;
    }

    // 2. Tomar el bus SPI0 (solo por unos microsegundos)
    if (!spi0_acquire_for_slow()) {
      return -1; // No se pudo tomar el mutex
    }

    // 3. Leer registro de datos (24 bits, tomamos los 16 MSB)
    uint8_t tx_cmd = AD4131_CMD_READ(AD4131_REG_DATA);
    uint8_t rx[3] = {0};

    gpio_put(PIN_CS_AD4131, 0);
    spi_write_blocking(PIN_ACQ_SPI_INST, &tx_cmd, 1);
    spi_read_blocking(PIN_ACQ_SPI_INST, 0x00, rx, 3);
    gpio_put(PIN_CS_AD4131, 1);

    // Convertir 24 bits a 16 bits (descartar los 8 LSB)
    data_buffer[ch] = (int16_t)((rx[0] << 8) | rx[1]);

    // 4. Restaurar bus y liberar mutex INMEDIATAMENTE
    spi0_release_for_slow();
  }

  return 0;
}
