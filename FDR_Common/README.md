# FDR_Common

Archivos compartidos entre el proyecto Maestro (ESP32-S3) y el Esclavo (RP2040-Zero).

Estos headers definen el **protocolo de comunicación SPI** y utilidades comunes

**No deben contener dependencias de ninguna plataforma específica**
(ni ESP-IDF ni Pico SDK), solo headers estándar de C (`stdint.h`, `stddef.h`).

## Archivos

| Archivo       | Descripción                                          |
| ------------- | ---------------------------------------------------- |
| `protocol.h`  | Estructuras de paquetes, tipos y payloads            |
| `crc_utils.h` | Función CRC32 portable (tabla por software)          |
