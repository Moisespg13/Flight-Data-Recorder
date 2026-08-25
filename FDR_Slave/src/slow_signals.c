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

// Mutex compartido para SPI0
static SemaphoreHandle_t s_spi0_mutex = NULL;

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
    uint16_t dummy_cmd = (0x800 /*WRITE=1*/ | (0 << 6) /*ADD=0*/ | 0x30 /*PM=11*/ | 0x01 /*CODING=1*/) << 4;
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

  // Enviar el comando inicial para el canal 0 (llena el pipeline)
  // Formato del Control Register (enviado en los 12 MSB):
  // Bit 11=WRITE, 10=SEQ(0), 9-6=ADD, 5-4=PM(11), 3=SHADOW(0), 2=WEAK(0), 1=RANGE(0), 0=CODING(1)
  uint16_t tx_cmd;
  uint16_t rx_data;
  
  tx_cmd = (0x800 | (0 << 6) | 0x30 | 0x01) << 4;
  gpio_put(PIN_CS_AD7490, 0);
  spi_write16_read16_blocking(PIN_ACQ_SPI_INST, &tx_cmd, &rx_data, 1);
  gpio_put(PIN_CS_AD7490, 1);
  
  // Pequeña pausa requerida por el AD7490 entre conversiones ("t_quiet")
  busy_wait_us(1);

  // Leer los 16 canales secuencialmente
  for (int ch = 0; ch < SLOW_SIGNALS_NUM_CHANNELS; ch++) {
    int next_ch = (ch + 1) % SLOW_SIGNALS_NUM_CHANNELS;
    
    // Preparar el comando para la *siguiente* conversión mientras leemos la *actual*
    tx_cmd = (0x800 | (next_ch << 6) | 0x30 | 0x01) << 4;
    
    gpio_put(PIN_CS_AD7490, 0);
    spi_write16_read16_blocking(PIN_ACQ_SPI_INST, &tx_cmd, &rx_data, 1);
    gpio_put(PIN_CS_AD7490, 1);
    
    busy_wait_us(1);

    // El resultado son los 12 LSB de la palabra de 16 bits (4 ceros a la izquierda)
    data_buffer[ch] = (int16_t)(rx_data & 0x0FFF);
  }

  // Restaurar bus y liberar mutex
  spi0_release_for_slow();

  return 0;
}
