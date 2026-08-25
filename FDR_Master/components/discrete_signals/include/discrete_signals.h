
/**
 * @file discrete_signals.h
 * @brief API pública del componente de lectura de señales discretas (TCA9555).
 *
 * Lee las 16 entradas digitales usando el expansor I2C TCA9555, liberando
 * 16 pines del ESP32-S3.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Inicializa los GPIO de entradas discretas como entradas con pull-down.
 * @return ESP_OK si fue exitoso.
 */
esp_err_t discrete_signals_init(void);

/**
 * @brief Lee el estado actual de las 16 entradas discretas.
 *
 * @param[out] states Puntero a uint16_t donde se almacenará el estado.
 *                    Bit 0 = DISC_0, Bit 1 = DISC_1, ..., Bit 15 = DISC_15.
 * @return ESP_OK si la lectura fue exitosa.
 */
esp_err_t discrete_signals_read(uint16_t *states);
