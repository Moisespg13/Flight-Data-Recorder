#pragma once // Garantiza que el archivo se incluya solo una vez

#include <stdint.h>

#define FDR_BYTE_INICIO     0xFD // Byte inicio: Para sincronización / Bandera
#define FDR_CARGA_MAXIMA    256 // Cantidad máxima de datos del buffer (carga útil)

// Tipos de Paquete
typedef enum {
    TIPO_PAQ_LATIDOS         = 0x00, // Heartbeat / Estoy vivo
    TIPO_PAQ_DATOS_RAPIDOS  = 0x01, // Entradas analógicas alta velocidad
    TIPO_PAQ_DATOS_LENTOS   = 0x02, // Entradas analógicas baja velocidad
    TIPO_PAQ_DATOS_DISCRETOS      = 0x03, // Entradas discretas (eventos GPIO)
    TIPO_PAQ_GNSS           = 0x10, // Datos de GPS/GNSS
    TIPO_PAQ_SALUD          = 0x20  // Telemetría de estado del sistema
} tipoPaquete_t;

// Cabecera Común (Formato TLV)
typedef struct __attribute__((packed)) {
    uint8_t byteInicio;     // 0xFD
    uint8_t tipo;           // tipoPaquete_t
    uint16_t secuencia;     // Contador monotónico
    uint64_t estampaTemporal;     // Estampa temporal UTC (nanosegundos)
    uint16_t longitud;      // Longitud de la carga útil
    // La carga útil sigue inmediatamente después
    // uint32_t crc32;      // CRC32 al final del paquete
} cabeceraFdr_t;

// Estructuras para cargas útiles específicas
typedef struct __attribute__((packed)) {
    int16_t canales[16];    // 16 canales * 16 bits
} cargaDatosRapidos_t;

