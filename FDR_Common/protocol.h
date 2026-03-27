
/**
 * @file protocol.h
 * @brief Definiciones del protocolo de comunicación inter-placa.
 *
 * Define las estructuras de paquetes (cabecera, tipos, payloads)
 * usadas para la comunicación SPI entre Maestro (ESP32-S3) y
 * Esclavo (RP2040-Zero). Archivo compartido entre ambos proyectos.
 *
 * IMPORTANTE: Este archivo NO debe incluir headers específicos de
 * ninguna plataforma (ni ESP-IDF ni Pico SDK).
 */

#pragma once

#include <stdint.h>

#define FDR_PROTO_MAGIC 0xFD // Byte inicio: Para sincronización / Bandera
#define FDR_MAX_PAYLOAD 256  // Cantidad máxima de datos del buffer (carga útil)

// Tipos de Paquete
typedef enum {
  PKT_TYPE_HEARTBEAT = 0x00, // Heartbeat
  PKT_TYPE_FAST_DATA = 0x01, // Entradas analógicas alta velocidad
  PKT_TYPE_SLOW_DATA = 0x02, // Entradas analógicas baja velocidad
  PKT_TYPE_DISCRETE = 0x03,  // Entradas discretas (GPIO)
  PKT_TYPE_GNSS = 0x10,      // Datos de GPS/GNSS
  PKT_TYPE_HEALTH = 0x20     // Estado del sistema
} packet_type_t;

// Cabecera Común (Formato TLV)
typedef struct __attribute__((packed)) {
  uint8_t magic;         // 0xFD
  uint8_t type;          // packet_type_t
  uint16_t sequence;     // Contador monotónico
  uint64_t timestamp_ns; // Estampa temporal UTC (nanosegundos)
  uint16_t length;       // Longitud de la carga útil
                         // La carga útil sigue inmediatamente después
                         // uint32_t crc32; // CRC32 al final del paquete
} fdr_header_t;

// Estructuras para cargas útiles específicas

// Payload: Señales rápidas (3000 Hz) - 40 bytes
typedef struct __attribute__((packed)) {
  uint64_t timestamp_ns; // Tiempo de adquisición (nanosegundos)
  int16_t channels[16];  // 16 canales * 16 bits
} fast_data_payload_t;

// Payload: Señales lentas (10 Hz) - 40 bytes
typedef struct __attribute__((packed)) {
  uint64_t timestamp_ns; // Tiempo de adquisición (nanosegundos)
  int16_t channels[16];  // 16 canales * 16 bits
} slow_data_payload_t;

// Payload: Señales digitales (20 Hz) - 10 bytes
typedef struct __attribute__((packed)) {
  uint64_t timestamp_ns; // Tiempo de adquisición (nanosegundos)
  uint16_t states;       // 16 señales digitales (1 bit por señal)
} discrete_payload_t;
