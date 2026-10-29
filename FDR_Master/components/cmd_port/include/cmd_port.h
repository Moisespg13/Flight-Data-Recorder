
/**
 * @file cmd_port.h
 * @brief API del Puerto de Comandos RS-232 (UART1).
 *
 * Implementa el protocolo Maestro-Esclavo definido en la especificación
 * del CEV para configuración y diagnóstico del MAD desde una notebook.
 *
 * Protocolo (mensajes de 6 bytes):
 *   [Header 16b] [Comando 16b] [Dato 16b]
 *
 * Comandos soportados:
 *   CMD 1: Lectura en tiempo real (ANA 0..31, DIG 32..47,
 *          IMU 48..60, tiempo GNSS 61..66, posición 67..71).
 *   CMD 2: Escritura del formato de muestreo en memoria no volátil.
 *   CMD 3: Lectura del formato de muestreo almacenado.
 *   CMD 4: Formatear MicroSD (requiere clave de confirmación 0x55AA).
 *
 * Conexión física:
 *   Notebook → Cable RS-232 → MAX3232E → ESP32 UART1 (GPIO41/42)
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

// Encabezado del protocolo (palabra mágica)
#define CMD_HEADER 0xAA55

// Códigos de comando
#define CMD_READ_PARAM   0x0001  // Lectura de parámetro en tiempo real
#define CMD_WRITE_FORMAT 0x0002  // Escritura de formato de muestreo a NVS
#define CMD_READ_FORMAT  0x0003  // Lectura de formato de muestreo desde NVS
#define CMD_FORMAT_SD    0x0004  // Formatear MicroSD completa

// Clave de seguridad para el comando de formateo
#define CMD_FORMAT_KEY   0x55AA

// Códigos de respuesta
#define CMD_ACK_OK       0x0001  // Confirmación exitosa
#define CMD_ACK_ERROR    0xFFFF  // Error

/**
 * @brief Estructura de un mensaje del protocolo de comandos.
 */
typedef struct __attribute__((packed)) {
  uint16_t header;   // CMD_HEADER (0xAA55)
  uint16_t command;  // Número de comando (1, 2 o 3)
  uint16_t data;     // Dato asociado al comando
} cmd_message_t;

/**
 * @brief Inicializa el UART del puerto de comandos.
 * @return ESP_OK si la inicialización fue exitosa.
 */
esp_err_t cmd_port_init(void);

/**
 * @brief Procesa mensajes pendientes del puerto de comandos.
 *
 * Lee el buffer UART, busca mensajes válidos (con header correcto),
 * ejecuta el comando y envía la respuesta. No bloqueante.
 *
 * @return ESP_OK si se procesó un comando, ESP_ERR_NOT_FOUND si no hay datos.
 */
esp_err_t cmd_port_process(void);

/**
 * @brief Actualiza el valor de un canal para lectura por CMD 1.
 *
 * Las tareas de adquisición llaman a esta función para mantener
 * actualizado el valor que se devuelve ante un CMD_READ_PARAM.
 *
 * @param channel Número de canal (0..31 analógicos, 32..47 discretas).
 *                CMD 1 también lee 48..71 (IMU, tiempo GNSS, posición) desde
 *                el ensamblador; esos no se actualizan por acá.
 * @param value   Valor actual del canal (raw 16 bits).
 */
void cmd_port_update_channel(uint8_t channel, int16_t value);
