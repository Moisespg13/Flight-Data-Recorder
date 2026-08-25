/**
 * @file pin_config.h
 * @brief Mapa de pines GPIO del Maestro (ESP32-S3-DevKitC-1).
 *
 * Centraliza todas las asignaciones de pines del Maestro.
 */

#pragma once

// ============================================================================
// SPI ENLACE INTER-PLACA (Maestro -> RP2040 Esclavo)
// ============================================================================
#define PIN_LINK_SPI_HOST SPI2_HOST
#define PIN_SPI_MISO 13
#define PIN_SPI_CLK  12
#define PIN_SPI_CS   10

// DATA READY (Slave -> Master)
#define PIN_SPI_DRDY 11

// ============================================================================
// I2C EXPANSOR DISCRETO (TCA9555)
// ============================================================================
#define PIN_I2C_SCL 4
#define PIN_I2C_SDA 5
#define I2C_NUM     0

// ============================================================================
// ALMACENAMIENTO (MicroSD SPI)
// ============================================================================
#define PIN_SD_MOSI 38
#define PIN_SD_CLK  39
#define PIN_SD_MISO 40
#define PIN_SD_CS1  1
#define PIN_SD_CS2  2

// ============================================================================
// GNSS (UART2 via GPIO Matrix)
// ============================================================================
#define PIN_GNSS_RX  35  // ESP32 RX <- GNSS TX
#define PIN_GNSS_TX  36  // ESP32 TX -> GNSS RX
#define PIN_PPS      37  // PPS del GNSS (Entrada)
#define GNSS_UART_NUM 2

// ============================================================================
// IMU XSENS MTi-28A53G35 (UART0)
// ============================================================================
#define PIN_IMU_RX   44  // ESP32 RX <- XSENS TX
#define PIN_IMU_TX   43  // ESP32 TX -> XSENS RX
#define IMU_UART_NUM 0

// ============================================================================
// PUERTO DE COMANDOS RS-232 (UART1 via GPIO Matrix)
// ============================================================================
#define PIN_CMD_RX   41  // ESP32 RX <- RS-232 TX
#define PIN_CMD_TX   42  // ESP32 TX -> RS-232 RX
#define CMD_UART_NUM 1

// ============================================================================
// DETECCIÓN DE PÉRDIDA DE ENERGÍA (POWER-LOSS)
// ============================================================================
#define PIN_POWER_LOSS 46

// ============================================================================
// LED INDICADOR
// ============================================================================
#define PIN_LED 0
