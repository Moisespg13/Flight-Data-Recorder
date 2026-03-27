
/**
 * @file spi_manager.h
 * @brief API pública del gestor de bus SPI del Maestro.
 *
 * Provee la función de inicialización del bus SPI Maestro
 * y acceso al handle del dispositivo para comunicación con el Esclavo.
 */

#pragma once

#include "driver/spi_master.h"
#include "esp_err.h"

/**
 * @brief Inicializa el bus SPI Maestro para enlace inter-placa.
 * @return ESP_OK si fue exitoso.
 */
esp_err_t spi_manager_init(void);

/**
 * @brief Devuelve el handle del dispositivo SPI Maestro.
 * @return Handle SPI para comunicación con el dispositivo Esclavo.
 */
spi_device_handle_t spi_manager_get_master_handle(void);
