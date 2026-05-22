
/**
 * @file fast_signals.h
 * @brief API del driver AD7606 para RP2040 (Señales Rápidas).
 *
 * Soporta 2 chips AD7606 (16 canales totales) conectados
 * al bus SPI0, cada uno con su propio CS y BUSY.
 *
 * El bus SPI0 se comparte con el AD4131 (señales lentas).
 * Ambos drivers usan un mutex de FreeRTOS para evitar colisiones.
 */

#pragma once

#include "FreeRTOS.h"
#include "semphr.h"
#include <stdint.h>

// Cantidad de chips AD7606 instalados (Luego modificar, ya que siempre serán 2)
#define FAST_SIGNALS_NUM_CHIPS 2

// Canales por chip AD7606
#define FAST_SIGNALS_CH_PER_CHIP 8

// Total de canales rápidos
#define FAST_SIGNALS_TOTAL_CH                                                  \
  (FAST_SIGNALS_NUM_CHIPS * FAST_SIGNALS_CH_PER_CHIP)

/**
 * @brief Inicializa SPI0 y los GPIOs de control de ambos AD7606.
 *
 * @param spi0_mutex Mutex compartido para el bus SPI0.
 */
void fast_signals_init(SemaphoreHandle_t spi0_mutex);

/**
 * @brief Resetea ambos AD7606 (pulso en pin RESET compartido).
 */
void fast_signals_reset(void);

/**
 * @brief Inicia la conversión en ambos AD7606 simultáneamente
 *        (pulso en pin CONVST compartido).
 */
void fast_signals_start_conversion(void);

/**
 * @brief Lee los 16 canales (8 de cada AD7606).
 *
 * Toma el mutex de SPI0 internamente antes de leer.
 *
 * Los canales se almacenan así:
 *   data_buffer[0..7]   -> AD7606 #1
 *   data_buffer[8..15]  -> AD7606 #2
 *
 * Si FAST_SIGNALS_NUM_CHIPS == 1, solo se llenan data_buffer[0..7]
 * y data_buffer[8..15] quedan en cero.
 *
 * @param[out] data_buffer Buffer de al menos FAST_SIGNALS_TOTAL_CH *
 * sizeof(int16_t).
 * @return 0 si exitoso, -1 si timeout en alguno de los chips.
 */
int fast_signals_read(int16_t *data_buffer);
