
/**
 * @file pin_config.h
 * @brief Mapa de pines GPIO del Esclavo (RP2040-Zero de Waveshare).
 *
 * La RP2040-Zero expone 20 pines en los bordes (GPIO 0-15, 26-29)
 * y un LED NeoPixel interno en GP16.
 *
 * Asignación de buses:
 *   - GP0-GP3:  Enlace inter-placa (PIO, Slave) -> Comunicación con ESP32-S3
 *   - GP4-GP7:  Bus de ADQUISICIÓN (SPI0, Master) -> AD7606, AD4131
 *   - GP8-GP15: Señales de control y sincronización
 *   - GP26-GP29: ADC control / sincronización
 *
 * Pinout consensuado con el equipo de Hardware (Mayo 2026).
 */

#pragma once

// ============================================================================
// SPI0 - Bus de ADQUISICIÓN (RP2040 como Master -> ADCs)
// ============================================================================
#define PIN_ACQ_SPI_INST  spi0
#define PIN_ACQ_MISO  4   // GP4  - SPI0 RX (datos del ADC)
#define PIN_ACQ_SCK   6   // GP6  - SPI0 SCK
#define PIN_ACQ_MOSI  7   // GP7  - SPI0 TX (datos al ADC, si aplica)

// Chip Selects de los ADCs (manejados como GPIO)
#define PIN_CS_AD7606_1   5   // GP5  - CS del primer AD7606 (8 canales rápidos)
#define PIN_CS_AD7606_2   27  // GP27 - CS del segundo AD7606 (8 canales rápidos)
#define PIN_CS_AD4131     9   // GP9  - CS del AD4131 (16 canales lentos)

// Señales de control del AD7606
#define PIN_ADC_CONVST    10  // GP10 - Conversion Start (compartido ambos AD7606)
#define PIN_ADC_BUSY      12  // GP12 - Busy (AD7606 #1)
#define PIN_ADC2_BUSY     28  // GP28 - Busy (AD7606 #2)
#define PIN_ADC_RST       15  // GP15 - Reset (compartido)

// Señales de control del AD4131 (ADC lento)
#define PIN_AD4131_DRDY   29  // GP29 - Data Ready (activo bajo)

// ============================================================================
// ENLACE INTER-PLACA (RP2040 como Slave <- ESP32-S3, vía PIO)
// ============================================================================
// NOTA: Estos pines NO usan el periférico SPI por hardware (PL022).
//       Se controlan mediante PIO (Programmable I/O) que funciona en
//       cualquier GPIO y permite implementar el protocolo SPI slave
//       de forma confiable.
#define PIN_LINK_CS   0   // GP0  - CS    (desde ESP32 Master)
#define PIN_LINK_TX   1   // GP1  - MISO  (datos hacia ESP32 Master)
#define PIN_LINK_SCK  2   // GP2  - Clock (desde ESP32 Master)
#define PIN_LINK_RX   3   // GP3  - MOSI  (datos desde ESP32 Master)

// ============================================================================
// SEÑALES DE SINCRONIZACIÓN
// ============================================================================
#define PIN_SYNC_TRIG     14  // GP14 - Entrada desde Maestro (arranca adquisición)
#define PIN_PPS           26  // GP26 - Entrada PPS desde GNSS

// ============================================================================
// CONTROL DEL SISTEMA
// ============================================================================
#define PIN_STBY          13  // GP13 - Pulsador Stand-By / Modo Descarga

// ============================================================================
// LED integrado (RP2040-Zero tiene un WS2812 en GP16)
// ============================================================================
#define PIN_LED_WS2812    16  // GP16 - LED NeoPixel integrado

// ============================================================================
// DATA READY (Slave -> Master)
// ============================================================================
#define PIN_DRDY          8   // GP8  - Señal DRDY: HIGH = paquete listo para leer

// ============================================================================
// PINES LIBRES (reservados para uso futuro)
// ============================================================================
// GP11 - Libre
