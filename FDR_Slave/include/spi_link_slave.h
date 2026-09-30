
/**
 * @file spi_link_slave.h
 * @brief API del enlace SPI Slave (RP2040 -> ESP32-S3).
 *
 * Inicializa SPI1 como Slave y provee funciones para enviar
 * paquetes de datos rápidos y lentos al Maestro, cada uno con
 * header + payload + CRC32.
 */

#pragma once

#include "protocol.h"

/**
 * @brief Inicializa SPI1 en modo Slave para comunicación con el Maestro.
 */
void spi_link_slave_init(void);

/**
 * @brief Acumula una muestra rápida. Cada 4 arma un paquete SPI.
 *
 * @param payload Puntero a una muestra (timestamp propio + 16 canales).
 */
void spi_link_slave_send_fast(const fast_data_payload_t *payload);

/**
 * @brief Envía un paquete de datos lentos (PKT_TYPE_SLOW_DATA) al Maestro.
 *
 * @param payload Puntero al payload de datos lentos.
 */
void spi_link_slave_send_slow(const slow_data_payload_t *payload);
