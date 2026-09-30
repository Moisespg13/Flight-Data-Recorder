/**
 * @file slow_signals.c
 * @brief Driver para el ADC AD7490 (Señales Lentas) - Pico SDK.
 *
 * Implementa la inicialización y lectura de 16 canales single-ended
 * del AD7490 vía SPI0 (compartido con los AD7606).
 *
 * El acceso al bus SPI0 se coordina con fast_signals mediante un
 * mutex de FreeRTOS. Antes de cada transacción, se reconfigura
 * el bus al Modo 3 (CPOL=1, CPHA=1) requerido por el AD7490,
 * y al finalizar se restaura al Modo 2 del AD7606.
 *
 * Protocolo SPI del AD7490:
 *   - Registros de 16 bits.
 *   - Los 12 MSB del comando enviados configuran el "Control Register".
 *   - Conversión síncrona: la respuesta del canal N llega mientras se
 *     envía el comando del canal N+1.
 */

#include "slow_signals.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "pin_config.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// Control Register del AD7490 (12 bits, se envían en los 12 MSB de la palabra)
// ============================================================================
//   bit 11: WRITE   (1 = cargar los 11 bits siguientes en el registro)
//   bit 10: SEQ
//   bit 9-6: ADD3..ADD0 (canal a convertir)
//   bit 5-4: PM1, PM0   (11 = modo normal)
//   bit 3:  SHADOW
//   bit 2:  Weak/TRI
//   bit 1:  RANGE       (0 = 0..2*VREF, 1 = 0..VREF)
//   bit 0:  CODING      (1 = binario recto, 0 = complemento a 2)
#define AD7490_CR_WRITE     (1u << 11)
#define AD7490_CR_SEQ       (1u << 10)
#define AD7490_CR_ADD(ch)   (((uint16_t)(ch) & 0x0Fu) << 6)
#define AD7490_CR_PM_NORMAL (3u << 4)
#define AD7490_CR_SHADOW    (1u << 3)
#define AD7490_CR_WEAK      (1u << 2)
#define AD7490_CR_RANGE     (1u << 1)
#define AD7490_CR_CODING    (1u << 0)

// El AD7490 está alimentado a 3.3 V. RANGE=0 (0 a 2*VREF) exige VDD >= 2*VREF,
// condición que no se cumple, así que forzamos RANGE=1: entrada de 0 a VREF.
// DEBE coincidir con el acondicionamiento de señal de la PCB.
#define AD7490_RANGE_SETTING AD7490_CR_RANGE

/**
 * @brief Arma la palabra de 16 bits que configura el canal @p channel.
 */
static inline uint16_t ad7490_cmd(int channel) {
  uint16_t cr = AD7490_CR_WRITE | AD7490_CR_ADD(channel) |
                AD7490_CR_PM_NORMAL | AD7490_RANGE_SETTING | AD7490_CR_CODING;
  return (uint16_t)(cr << 4);
}

// Mutex compartido para SPI0
static SemaphoreHandle_t s_spi0_mutex = NULL;

// Lecturas descartadas porque el canal devuelto por el ADC no coincidía con
// el esperado (pipeline desalineado).
static volatile uint32_t s_desync_count = 0;

/**
 * @brief Reconfigura SPI0 al Modo 3 (AD7490) y toma el mutex.
 * @return true si se pudo tomar el mutex, false si timeout.
 */
static bool spi0_acquire_for_slow(void) {
  if (xSemaphoreTake(s_spi0_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;
  }
  // Cambiar a Modo 3 (CPOL=1, CPHA=1) para AD7490 y transferencias de 16 bits
  spi_set_format(PIN_ACQ_SPI_INST, 16, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);
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

void slow_signals_init(SemaphoreHandle_t spi0_mutex) {
  s_spi0_mutex = spi0_mutex;

  // Configurar CS del AD7490 como GPIO (salida, inactivo = alto)
  gpio_init(PIN_CS_AD7490);
  gpio_set_dir(PIN_CS_AD7490, GPIO_OUT);
  gpio_put(PIN_CS_AD7490, 1);

  // Ya no usamos DRDY.
  
  // Realizar dos transferencias dummy al iniciar para inicializar el pipeline
  if (spi0_acquire_for_slow()) {
    uint16_t dummy_cmd = ad7490_cmd(0);
    uint16_t rx;
    
    gpio_put(PIN_CS_AD7490, 0);
    spi_write16_read16_blocking(PIN_ACQ_SPI_INST, &dummy_cmd, &rx, 1);
    gpio_put(PIN_CS_AD7490, 1);
    
    busy_wait_us(1);
    
    gpio_put(PIN_CS_AD7490, 0);
    spi_write16_read16_blocking(PIN_ACQ_SPI_INST, &dummy_cmd, &rx, 1);
    gpio_put(PIN_CS_AD7490, 1);

    spi0_release_for_slow();

    printf("[SLOW] AD7490 inicializado (%d canales, pipeline preparado).\n",
           SLOW_SIGNALS_NUM_CHANNELS);
  } else {
    printf("[SLOW] *** ERROR: No se pudo tomar el mutex SPI0! ***\n");
  }
}

int slow_signals_read(int16_t *data_buffer) {
  if (data_buffer == NULL) {
    return -1;
  }

  // Tomar el bus SPI0
  if (!spi0_acquire_for_slow()) {
    return -1; // No se pudo tomar el mutex
  }

  // Trama de cebado: convierte el canal que quedó direccionado por la lectura
  // anterior y su resultado se descarta. Deja el pipeline alineado de modo que
  // la trama i del bucle devuelva la conversión del canal i.
  uint16_t tx_cmd = ad7490_cmd(0);
  uint16_t rx_data;

  gpio_put(PIN_CS_AD7490, 0);
  spi_write16_read16_blocking(PIN_ACQ_SPI_INST, &tx_cmd, &rx_data, 1);
  gpio_put(PIN_CS_AD7490, 1);

  // Pequeña pausa requerida por el AD7490 entre conversiones ("t_quiet")
  busy_wait_us(1);

  // Leer los 16 canales secuencialmente
  bool desync = false;
  for (int ch = 0; ch < SLOW_SIGNALS_NUM_CHANNELS; ch++) {
    int next_ch = (ch + 1) % SLOW_SIGNALS_NUM_CHANNELS;

    // Preparar el comando para la *siguiente* conversión mientras leemos la *actual*
    tx_cmd = ad7490_cmd(next_ch);

    gpio_put(PIN_CS_AD7490, 0);
    spi_write16_read16_blocking(PIN_ACQ_SPI_INST, &tx_cmd, &rx_data, 1);
    gpio_put(PIN_CS_AD7490, 1);

    busy_wait_us(1);

    // Los 4 MSB traen el ADD del canal convertido. Verificarlo es lo único
    // que distingue una lectura correcta de un pipeline corrido una posición,
    // que de otro modo produce 16 valores plausibles pero mal atribuidos.
    if (((rx_data >> 12) & 0x0F) != (uint16_t)ch) {
      desync = true;
    }

    // El resultado son los 12 LSB de la palabra de 16 bits
    data_buffer[ch] = (int16_t)(rx_data & 0x0FFF);
  }

  // Restaurar bus y liberar mutex
  spi0_release_for_slow();

  if (desync) {
    s_desync_count++;
    if (s_desync_count <= 10 || s_desync_count % 100 == 0) {
      printf("[SLOW] WARN: canal inesperado del AD7490 (#%lu descartes).\n",
             (unsigned long)s_desync_count);
    }
    // Descartamos la lectura completa. La trama de cebado del próximo ciclo
    // vuelve a alinear el pipeline.
    return -1;
  }

  return 0;
}
