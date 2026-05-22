
/**
 * @file imu_xsens.h
 * @brief API del driver para el sensor IMU XSENS MTi-28A53G35.
 *
 * Maneja la comunicación UART con el sensor IMU, parsea las tramas
 * del protocolo MT Communication Protocol y extrae los 12 parámetros:
 *   - Aceleración (X, Y, Z)
 *   - Giróscopo (X, Y, Z)
 *   - Magnetómetro (X, Y, Z)
 *   - Ángulos de Euler (Roll, Pitch, Yaw)
 *
 * Protocolo XSENS MT:
 *   Cada trama tiene la estructura:
 *     [0xFA] [0xFF] [MsgID] [Length] [Data...] [Checksum]
 *
 *   El mensaje principal es MTData (ID=0x32) que contiene los
 *   12 floats empaquetados en big-endian.
 */

#pragma once

#include "esp_err.h"
#include "protocol.h"
#include <stdbool.h>

/**
 * @brief Inicializa el UART y el driver del sensor XSENS.
 *
 * Configura el UART (por defecto UART0 a 115200 baud) y prepara
 * el parser de tramas MT.
 *
 * @return ESP_OK si la inicialización fue exitosa.
 */
esp_err_t imu_xsens_init(void);

/**
 * @brief Lee y parsea datos pendientes del sensor (no bloqueante).
 *
 * Debe llamarse periódicamente desde una tarea. Lee bytes del
 * buffer UART y busca tramas MT válidas.
 *
 * @return ESP_OK si se procesó una trama, ESP_ERR_NOT_FOUND si no hay datos.
 */
esp_err_t imu_xsens_process(void);

/**
 * @brief Obtiene los últimos 12 parámetros de la IMU.
 *
 * @param[out] payload Estructura con los 12 valores y timestamp.
 * @return true si los datos son válidos (se recibió al menos una trama).
 */
bool imu_xsens_get_data(imu_payload_t *payload);
