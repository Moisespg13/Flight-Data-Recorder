
/**
 * @file pin_config.h
 * @brief Mapa de pines GPIO del Esclavo (RP2040-Zero de Waveshare).
 *
 * La RP2040-Zero expone 20 pines en los bordes (GPIO 0-15, 26-29)
 * y algunos en la parte inferior. Esta configuración asigna los
 * pines para los dos buses SPI:
 *   - SPI0: Bus de ADQUISICIÓN (Master) -> AD7606, AD4131
 *   - SPI1: Bus de ENLACE (Slave) -> Comunicación con ESP32-S3
 *
 * TODO: Ajustar los números de GPIO según el PCB final.
 */

#pragma once

// ============================================================================
// SPI0 - Bus de ADQUISICIÓN (RP2040 como Master -> ADCs)
// ============================================================================
#define PIN_ACQ_SPI_INST  spi0
#define PIN_ACQ_SCK   2   // GP2  - Clock
#define PIN_ACQ_MOSI  3   // GP3  - Master Out (datos al ADC, si aplica)
#define PIN_ACQ_MISO  4   // GP4  - Master In  (datos del ADC)

// Chip Selects de los ADCs
#define PIN_CS_AD7606_1   5   // GP5  - CS del primer AD7606
#define PIN_CS_AD7606_2   6   // GP6  - CS del segundo AD7606
#define PIN_CS_AD4131     7   // GP7  - CS del AD4131

// Señales de control del AD7606
#define PIN_ADC_CONVST    8   // GP8  - Conversion Start (CONVST A+B)
#define PIN_ADC_BUSY      9   // GP9  - Busy (ADC 1)
#define PIN_ADC2_BUSY     10  // GP10 - Busy (ADC 2)
#define PIN_ADC_RST       11  // GP11 - Reset (compartido)

// ============================================================================
// SPI1 - Bus de ENLACE INTER-PLACA (RP2040 como Slave <- ESP32-S3)
// ============================================================================
#define PIN_LINK_SPI_INST spi1
#define PIN_LINK_SCK  14  // GP14 - Clock (desde ESP32 Master)
#define PIN_LINK_MOSI 15  // GP15 - MOSI  (desde ESP32 Master)
#define PIN_LINK_MISO 12  // GP12 - MISO  (hacia ESP32 Master)
#define PIN_LINK_CS   13  // GP13 - CS    (desde ESP32 Master)

// ============================================================================
// SEÑALES DE SINCRONIZACIÓN
// ============================================================================
#define PIN_SYNC_TRIG     0   // GP0  - Entrada desde Maestro (arranca adquisición)
#define PIN_PPS           1   // GP1  - Entrada PPS desde GNSS/Maestro

// ============================================================================
// LED integrado (RP2040-Zero tiene un WS2812 en GP16)
// ============================================================================
#define PIN_LED_WS2812    16  // GP16 - LED NeoPixel integrado
