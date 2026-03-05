
/**
 * @file pin_config.h
 * @brief Mapa de pines GPIO para ambos roles (Maestro y Esclavo).
 *
 * Centraliza todas las asignaciones de pines del proyecto, separadas
 * por rol via Kconfig. Incluye pines SPI, ADC, almacenamiento,
 * GNSS, chip selects y señales de sincronización.
 */

#pragma once // Garantiza que el archivo se incluya solo una vez

#include "sdkconfig.h" // Configuración del proyecto

// --- Pines SPI ---
// Cambiar estos valores para que coincidan con el PCB!
#ifdef CONFIG_FDR_ROLE_MASTER
#define PIN_SPI_MISO 13 // Ejemplo
#define PIN_SPI_MOSI 11 // Ejemplo
#define PIN_SPI_CLK 12  // Ejemplo
#define PIN_SPI_CS 10   // Chip Select del Maestro

// Pines de almacenamiento (SDMMC/SPI)
#define PIN_SD_CMD 38 // Ejemplo
#define PIN_SD_CLK 39 // Ejemplo
#define PIN_SD_D0 40  // Ejemplo

// Pines de GNSS
#define PIN_GNSS_RX 5 // Ejemplo
#define PIN_GNSS_TX 6 // Ejemplo
#define PIN_PPS 7     // PPS del GNSS (Entrada en Maestro)

// Señales de Sincronización Inter-Placa
#define PIN_SYNC_TRIG 9 // Salida en Maestro -> Entrada en Esclavo

#elif defined(CONFIG_FDR_ROLE_SLAVE)
// --- Bus de COMUNICACIÓN INTER-PLACA (SPI Slave) ---
// ESP32 ESCLAVO. Bus Link dedicado.
#define PIN_LINK_SPI_HOST SPI3_HOST
#define PIN_LINK_MISO 13
#define PIN_LINK_MOSI 11
#define PIN_LINK_CLK 12
#define PIN_LINK_CS 10

// --- Bus de ADQUISICIÓN UNIFICADO (SPI Master) ---
// Todo conectado a un solo bus (AD7606 x2, AD4313, MAX22190 x2)
// MOVIDO para liberar USB nativo (GPIO 19/20) para Shell/MSC.
#define PIN_ACQ_SPI_HOST SPI2_HOST
#define PIN_ACQ_CLK 48  // Antes 18
#define PIN_ACQ_MISO 47 // Antes 19 (USB D-)
#define PIN_ACQ_MOSI 21 // Antes 20 (USB D+)

// Chip Selects (Uno por chip, salvo que uses Daisy Chain)
#define PIN_CS_AD7606_1 4
#define PIN_CS_AD7606_2 5
#define PIN_CS_AD4131 6
// Reservado para MAX22190 (Digital In)
// #define PIN_CS_MAX_1 7
// #define PIN_CS_MAX_2 8

// Señales de Control AD7606
#define PIN_ADC_CNV 15
#define PIN_ADC_BUSY 16
#define PIN_ADC2_BUSY 14 // Nuevo BUSY para el 2do ADC (GPIO 14)
#define PIN_ADC_RST 17

// Señales de Sincronización
#define PIN_SYNC_TRIG 9 // Entrada desde Maestro (Arranca adquisición)
#define PIN_PPS 7       // Entrada desde GNSS/Maestro (Sincronización fina)
#endif

/*#define PIN_SPI_MISO 13            // Ejemplo
#define PIN_SPI_MOSI 11            // Ejemplo
#define PIN_SPI_CLK 12             // Ejemplo
#define PIN_SPI_CS 10              // Chip Select del Esclavo

// Pines de adquisición (AD7606 - SPI Mode)
#define PIN_ADC_CNV 15             // Disparo de conversión (CONVST A+B)
#define PIN_ADC_BUSY 16            // Estado de conversión (ESTÁ A 5V!)
#define PIN_ADC_RST 17             // Reset del ADC
// Mapeo de datos para Dual SPI
#define PIN_ADC_DOUTA PIN_SPI_MISO // Canales 1-4 (GPIO 13) (ESTÁ A 5V!)
#define PIN_ADC_DOUTB PIN_SPI_MOSI // Canales 5-8 (GPIO 11) (ESTÁ A 5V!)*/