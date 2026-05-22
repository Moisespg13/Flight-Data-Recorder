
/**
 * @file slow_signals.h
 * @brief API del driver AD4131 para RP2040 (Señales Lentas).
 *
 * Soporta un chip AD4131-8 (16 canales single-ended) conectado
 * al bus SPI0 compartido con los AD7606. Ambos drivers deben
 * coordinarse mediante un mutex para evitar colisiones.
 *
 * El AD4131 opera en SPI Modo 3 (CPOL=1, CPHA=1), diferente al
 * Modo 2 del AD7606. La reconfiguración del bus se realiza
 * automáticamente al tomar el mutex.
 */

#pragma once

#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"

// Canales del AD4131 (single-ended)
#define SLOW_SIGNALS_NUM_CHANNELS  16

/**
 * @brief Inicializa los GPIOs de control del AD4131 (CS, DRDY).
 *
 * NOTA: NO inicializa SPI0 (ya lo hace fast_signals_init).
 *       Solo configura CS y DRDY como GPIO.
 *
 * @param spi0_mutex Mutex compartido para el bus SPI0.
 */
void slow_signals_init(SemaphoreHandle_t spi0_mutex);

/**
 * @brief Lee los 16 canales del AD4131.
 *
 * Toma el mutex de SPI0, reconfigura el bus a Modo 3,
 * realiza las lecturas, restaura el bus a Modo 2 y libera el mutex.
 *
 * @param[out] data_buffer Buffer de al menos SLOW_SIGNALS_NUM_CHANNELS int16_t.
 * @return 0 si exitoso, -1 si timeout en DRDY o no se pudo tomar el mutex.
 */
int slow_signals_read(int16_t *data_buffer);
