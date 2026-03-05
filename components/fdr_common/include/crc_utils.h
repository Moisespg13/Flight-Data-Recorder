
/**
 * @file crc_utils.h
 * @brief Utilidad de cálculo CRC32 usando el acelerador ROM del ESP32.
 *
 * Provee una función para calcular CRC32 de manera eficiente,
 * utilizada para verificar la integridad de los paquetes SPI.
 */

#pragma once

#include "esp_rom_crc.h" // Funciones de ROM optimizadas
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Calcula el CRC32 de un buffer (Little Endian, standard Ethernet/ZIP)
 *
 * @param parsed_buf Buffer de datos
 * @param len Longitud en bytes
 * @return uint32_t Valor CRC32
 */
static inline uint32_t fdr_calc_crc32(const uint8_t *parsed_buf, size_t len) {
  // Usamos la implementación por hardware/ROM del ESP32
  // Init val = 0xFFFFFFFF (estándar), pero la funcion de ROM toma ~val
  // asi que pasamos 0 para init (funciona directament como CRC32 normal)
  return esp_rom_crc32_le(0, parsed_buf, len);
}
