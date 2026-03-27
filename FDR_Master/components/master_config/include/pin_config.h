
/**
 * @file pin_config.h
 * @brief Mapa de pines GPIO del Maestro (ESP32-S3).
 *
 * Centraliza todas las asignaciones de pines del Maestro:
 * - SPI de enlace inter-placa (hacia RP2040)
 * - Entradas discretas (lectura directa GPIO, sin MAX22190)
 * - Almacenamiento (SD/eMMC)
 * - GNSS
 * - Señales de sincronización
 */

#pragma once

// ============================================================================
// SPI ENLACE INTER-PLACA (Maestro -> RP2040 Esclavo)
// ============================================================================
#define PIN_LINK_SPI_HOST SPI2_HOST
#define PIN_SPI_MISO 13
#define PIN_SPI_MOSI 11
#define PIN_SPI_CLK  12
#define PIN_SPI_CS   10

// ============================================================================
// ENTRADAS DISCRETAS (Lectura directa por GPIO)
// ============================================================================
// 16 entradas discretas mapeadas a GPIOs del ESP32-S3
// TODO: Ajustar números de GPIO según PCB final
#define PIN_DISC_0   1
#define PIN_DISC_1   2
#define PIN_DISC_2   3
#define PIN_DISC_3   4
#define PIN_DISC_4   5
#define PIN_DISC_5   6
#define PIN_DISC_6   7
#define PIN_DISC_7   8
#define PIN_DISC_8   14
#define PIN_DISC_9   15
#define PIN_DISC_10  16
#define PIN_DISC_11  17
#define PIN_DISC_12  18
#define PIN_DISC_13  21
#define PIN_DISC_14  47
#define PIN_DISC_15  48

// Máscara de bits para gpio_config de todas las discretas
#define PIN_DISC_MASK ( \
    (1ULL << PIN_DISC_0)  | (1ULL << PIN_DISC_1)  | \
    (1ULL << PIN_DISC_2)  | (1ULL << PIN_DISC_3)  | \
    (1ULL << PIN_DISC_4)  | (1ULL << PIN_DISC_5)  | \
    (1ULL << PIN_DISC_6)  | (1ULL << PIN_DISC_7)  | \
    (1ULL << PIN_DISC_8)  | (1ULL << PIN_DISC_9)  | \
    (1ULL << PIN_DISC_10) | (1ULL << PIN_DISC_11) | \
    (1ULL << PIN_DISC_12) | (1ULL << PIN_DISC_13) | \
    (1ULL << PIN_DISC_14) | (1ULL << PIN_DISC_15))

// Array de pines discretos (para iterar)
#define PIN_DISC_LIST { \
    PIN_DISC_0,  PIN_DISC_1,  PIN_DISC_2,  PIN_DISC_3,  \
    PIN_DISC_4,  PIN_DISC_5,  PIN_DISC_6,  PIN_DISC_7,  \
    PIN_DISC_8,  PIN_DISC_9,  PIN_DISC_10, PIN_DISC_11, \
    PIN_DISC_12, PIN_DISC_13, PIN_DISC_14, PIN_DISC_15  \
}

#define NUM_DISCRETE_INPUTS 16

// ============================================================================
// ALMACENAMIENTO (SDMMC/SPI)
// ============================================================================
#define PIN_SD_CMD 38
#define PIN_SD_CLK 39
#define PIN_SD_D0  40

// ============================================================================
// GNSS
// ============================================================================
#define PIN_GNSS_RX 35
#define PIN_GNSS_TX 36
#define PIN_PPS     37  // PPS del GNSS (Entrada)

// ============================================================================
// SEÑALES DE SINCRONIZACIÓN INTER-PLACA
// ============================================================================
#define PIN_SYNC_TRIG 9  // Salida: Maestro -> Esclavo (arranca adquisición)
