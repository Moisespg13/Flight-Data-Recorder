# master_config — Configuración Global de Hardware y Estado

Componente que centraliza la definición del mapa de pines del SoC ESP32-S3, parámetros globales de temporización y el control del indicador LED de estado del sistema. No contiene tareas de procesamiento de datos.

---

## Archivo Central de Pines (`pin_config.h`)

Define las asignaciones definitivas de los terminales GPIO para todos los periféricos de la placa maestra:

| Periférico | Señal | GPIO | Notas de Diseño |
|------------|-------|:----:|-----------------|
| **Enlace SPI2 (RP2040)** | CS | 10 | Salida Chip Select hacia RP2040 (GP0) |
| | DRDY | 11 | Entrada Data Ready (reemplaza pin de strapping 45) |
| | SCK | 12 | Salida Clock maestro (10 MHz) |
| | MISO | 13 | Entrada datos serie desde RP2040 |
| **MicroSD (SPI3)** | CS1 | 1 | Chip Select MicroSD #1 |
| | CS2 | 2 | Chip Select MicroSD #2 |
| | MOSI | 38 | Master Out Slave In |
| | SCK | 39 | Reloj SPI (20 MHz) |
| | MISO | 40 | Master In Slave Out compartido |
| **GNSS** | PPS | 15 | Pulso por segundo de sincronización |
| | TX | 16 | Salida de configuración |
| | RX | 17 | Entrada NMEA (9600 baudios) |
| **IMU Xsens** | TX | 43 | Salida serie |
| | RX | 44 | Entrada de telemetría inercial (115200 baudios) |
| **Comandos RS-232** | RX | 41 | Entrada TTL desde MAX3232 |
| | TX | 42 | Salida TTL hacia MAX3232 |
| **Discretas (I2C0)** | SCL | 4 | Reloj I2C hacia TCA9555 |
| | SDA | 5 | Datos I2C hacia TCA9555 |
| **Alimentación / Reset**| PWR_LOST | 46 | Interrupción por pérdida de alimentación principal |
| **USB Host MSC** | D- / D+ | 19 / 20 | Líneas USB nativas para descarga de datos |
| **LED Indicador** | LED_STATUS | 0 | Salida a LED testigo de placa (activo en bajo) |
| **PSRAM Octal** | D0..D7, CLK, CS | 33–37 | Bus octal interno del encapsulado N16R8 |

---

## Patrones del Indicador LED (`led_indicator.c`)

El LED testigo en GPIO 0 es comandado por una tarea de baja prioridad (`tsk_led`, prioridad 1 en Núcleo 0) para informar visualmente al operador sobre el estado del registrador:

* **Modo Vuelo Normal:** Parpadeo lento y regular a 1 Hz (500 ms encendido, 500 ms apagado) indicando grabación activa en las tarjetas MicroSD.
* **Modo Descarga (USB Host MSC):** Parpadeo rápido a 5 Hz durante la transferencia de archivos al pendrive.
* **Falla Crónica / Alerta:** Encendido continuo o secuencias de ráfaga rápida indicando error de inicialización en ambas tarjetas MicroSD o falla irrecuperable de periférico esencial.
