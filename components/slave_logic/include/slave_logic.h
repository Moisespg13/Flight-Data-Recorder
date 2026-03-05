
/**
 * @file slave_logic.h
 * @brief API pública del componente de lógica del Esclavo.
 *
 * Contiene slave_logic_start() para arrancar la adquisición de datos
 * y la transmisión al Maestro vía SPI.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Inicializa y arranca la lógica del Esclavo (Adquisición).
 *        Configura tareas, colas y drivers de sensores.
 *
 * @return esp_err_t ESP_OK si todo arrancó bien.
 */
esp_err_t slave_logic_start(void);
