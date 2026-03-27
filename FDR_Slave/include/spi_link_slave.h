
/**
 * @file spi_link_slave.h
 * @brief API del enlace SPI Slave (RP2040 -> ESP32-S3).
 *
 * Inicializa SPI1 como Slave y provee función para enviar
 * paquetes con header + payload + CRC al Maestro.
 */

#pragma once

#include "protocol.h"

/**
 * @brief Inicializa SPI1 en modo Slave para comunicación con el Maestro.
 */
void spi_link_slave_init(void);

/**
 * @brief Envía un paquete fast_data al Maestro.
 *        Arma el paquete completo (header + payload + CRC32) y lo carga
 *        en el buffer de transmisión SPI Slave.
 *
 * @param payload Puntero al payload de datos rápidos a enviar.
 */
void spi_link_slave_send(const fast_data_payload_t *payload);
