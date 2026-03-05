
/**
 * @file spi_manager.h
 * @brief API pública del gestor de bus SPI.
 *
 * Provee la función de inicialización del bus SPI.
 */

#pragma once

#include "esp_err.h"
#include "sdkconfig.h" // IWYU pragma: keep

/**
 * @brief Inicializa el bus SPI según el rol configurado (Maestro o Esclavo).
 *
 * Retorna ESP_OK si fue exitoso.
 */

esp_err_t spi_manager_init(void);

#ifdef CONFIG_FDR_ROLE_MASTER
#include "driver/spi_master.h"
// Devuelve el handle del dispositivo SPI Maestro (para comunicación con
// Esclavo)
spi_device_handle_t spi_manager_get_master_handle(void);
#endif
