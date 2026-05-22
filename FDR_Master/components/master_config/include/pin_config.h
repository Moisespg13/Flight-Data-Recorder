
/**
 * @file pin_config.h
 * @brief Mapa de pines GPIO del Maestro (ESP32-S3-DevKitC-1).
 *
 * Centraliza todas las asignaciones de pines del Maestro.
 * Las entradas discretas están numeradas en orden físico de la placa
 * (bajando por el lado izquierdo, luego por el lado derecho).
 *
 * NOTA: El ESP32-S3 posee una Matriz GPIO que permite asignar
 * cualquier periférico (UART, SPI, I2C) a casi cualquier pin.
 * Por eso los UARTs no necesitan estar en sus pines "por defecto".
 *
 * Pinout consensuado con el equipo de Hardware (Mayo 2026).
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
// 16 entradas discretas, numeradas en orden físico de la placa.
//
// Lado izquierdo (de arriba hacia abajo):
#define PIN_DISC_0   4   // GPIO4  - Pin físico L4
#define PIN_DISC_1   5   // GPIO5  - Pin físico L5
#define PIN_DISC_2   6   // GPIO6  - Pin físico L6
#define PIN_DISC_3   7   // GPIO7  - Pin físico L7
#define PIN_DISC_4   15  // GPIO15 - Pin físico L8
#define PIN_DISC_5   16  // GPIO16 - Pin físico L9
#define PIN_DISC_6   17  // GPIO17 - Pin físico L10
#define PIN_DISC_7   18  // GPIO18 - Pin físico L11
#define PIN_DISC_8   8   // GPIO8  - Pin físico L12
#define PIN_DISC_9   3   // GPIO3  - Pin físico L13 (⚠️ STRAP pin)
#define PIN_DISC_10  14  // GPIO14 - Pin físico L20
// Lado derecho (de arriba hacia abajo):
#define PIN_DISC_11  1   // GPIO1  - Pin físico R4
#define PIN_DISC_12  2   // GPIO2  - Pin físico R5
#define PIN_DISC_13  48  // GPIO48 - Pin físico R16
#define PIN_DISC_14  47  // GPIO47 - Pin físico R17
#define PIN_DISC_15  21  // GPIO21 - Pin físico R18

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
// GNSS (UART2 via GPIO Matrix)
// ============================================================================
#define PIN_GNSS_RX  35  // ESP32 RX <- GNSS TX
#define PIN_GNSS_TX  36  // ESP32 TX -> GNSS RX
#define PIN_PPS      37  // PPS del GNSS (Entrada)
#define GNSS_UART_NUM 2  // UART2

// ============================================================================
// IMU XSENS MTi-28A53G35 (UART0)
// ============================================================================
// NOTA: UART0 es compartido con la consola de debug durante desarrollo.
//       Para producción, redirigir consola a USB-CDC en menuconfig:
//       Component config -> ESP System Settings -> Channel for console output
//       -> USB Serial/JTAG
#define PIN_IMU_RX   44  // ESP32 RX <- XSENS TX (GPIO44 = UART0 RX default)
#define PIN_IMU_TX   43  // ESP32 TX -> XSENS RX (GPIO43 = UART0 TX default)
#define IMU_UART_NUM 0   // UART0

// ============================================================================
// PUERTO DE COMANDOS RS-232 (UART1 via GPIO Matrix)
// ============================================================================
// Conexión a notebook del técnico vía conversor RS-232 <-> TTL
#define PIN_CMD_RX   41  // ESP32 RX <- RS-232 TX
#define PIN_CMD_TX   42  // ESP32 TX -> RS-232 RX
#define CMD_UART_NUM 1   // UART1

// ============================================================================
// SEÑALES DE SINCRONIZACIÓN INTER-PLACA
// ============================================================================
#define PIN_SYNC_TRIG 9  // Salida: Maestro -> Esclavo (arranca adquisición)

// ============================================================================
// DETECCIÓN DE PÉRDIDA DE ENERGÍA (POWER-LOSS)
// ============================================================================
// Señal proveniente de un comparador de voltaje externo que monitorea
// la línea de 28V del avión. Normalmente HIGH. Baja a LOW cuando la
// tensión cae por debajo del umbral seguro (~22V), indicando un corte
// inminente de energía. Los capacitores de hold-up deben mantener
// la alimentación del MCU por al menos 100ms para completar el cierre
// del archivo en la MicroSD.
// GPIO 46 es input-only en el ESP32-S3 (ideal para esta función).
#define PIN_POWER_LOSS 46

// ============================================================================
// USB HOST (Modo Descarga - Pendrive)
// ============================================================================
// GPIO19 y GPIO20 están conectados internamente al controlador USB
// del ESP32-S3. No necesitan definición explícita, el driver USB
// los configura automáticamente. Se documentan aquí como referencia.
// #define PIN_USB_DP  20  // USB D+
// #define PIN_USB_DM  19  // USB D-

// ============================================================================
// DATA READY (Slave -> Master)
// ============================================================================
// GPIO45 es STRAP pin (selecciona voltaje VDD_SPI al bootear).
// Es seguro usarlo como input después del boot porque DRDY arranca LOW
// (el esclavo no aserta DRDY hasta recibir SYNC_TRIG).
#define PIN_SPI_DRDY 45

// ============================================================================
// PINES LIBRES
// ============================================================================
// GPIO0  - Libre (⚠️ STRAP pin, usar con precaución)
