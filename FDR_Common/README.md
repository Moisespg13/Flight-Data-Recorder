# FDR_Common — Definiciones de Protocolo y Utilidades Comunes

Directorio que centraliza las estructuras de datos, constantes de protocolo y utilidades portables compartidas entre el firmware de la Placa Maestra (**ESP32-S3**) y la Placa Esclava (**RP2040-Zero**).

> **Regla de Portabilidad:** Los archivos contenidos en esta carpeta no deben incluir librerías ni dependencias dependientes de una plataforma o SDK en particular (como ESP-IDF o Raspberry Pi Pico SDK). Solo se permiten encabezados estándar de C99 (`stdint.h`, `stddef.h`, `stdbool.h`).

---

## Estructura de Archivos

| Archivo | Descripción |
|---------|-------------|
| [protocol.h](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Common/protocol.h) | Definición binaria de los paquetes del enlace serie SPI inter-placa (encabezado común, cargas útiles rápidas y lentas). |
| [crc_utils.h](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Common/crc_utils.h) | Implementación de cálculo de redundancia cíclica CRC-32 estándar mediante tabla precalculada por software. |

---

## Formato del Paquete de Comunicación SPI

Todos los paquetes transmitidos por la placa esclava a través del enlace PIO/DMA hacia la maestra responden al siguiente empaquetado binario:

```
+-------------------------------------------------------------------------------+
|  Header (14 bytes)  |             Payload (N bytes)            | CRC32 (4 B) |
+---------------------+------------------------------------------+-------------+
```

### Encabezado Común (`fdr_header_t` - 14 bytes)
* **`magic` (1 byte):** Byte de preámbulo fijo `0xFD` (`FDR_PROTO_MAGIC`). Permite sincronizar tramas y validar el inicio de paquete.
* **`type` (1 byte):** Tipo de paquete (`PKT_TYPE_FAST_DATA = 0x01` o `PKT_TYPE_SLOW_DATA = 0x02`).
* **`sequence` (2 bytes):** Contador incremental de secuencia por tipo de paquete para detección de pérdidas de tramas.
* **`timestamp_ns` (8 bytes):** Marca de tiempo en nanosegundos disciplinada por el pulso PPS del GNSS.
* **`length` (2 bytes):** Longitud en bytes de la carga útil que sigue al encabezado.

### Carga Útil Rápida (`fast_data_spi_payload_t` - 160 bytes)
* Contiene **4 muestras consecutivas** (`FAST_SAMPLES_PER_SPI_PKT = 4`).
* Cada muestra (`fast_data_payload_t` - 40 bytes) transporta:
  * `timestamp_ns` (8 bytes): Estampa de tiempo exacta de la conversión particular.
  * `channels[16]` (32 bytes): Valores de 16 bits en complemento a dos correspondientes a las 16 entradas analógicas rápidas (AD7606 x2).
* **Longitud total del paquete rápido:** 14 + 160 + 4 = **178 bytes**.

### Carga Útil Lenta (`slow_data_payload_t` - 40 bytes)
* Transporta 1 muestra de 16 canales analógicos lentos (AD7490):
  * `timestamp_ns` (8 bytes): Estampa temporal de la captura.
  * `channels[16]` (32 bytes): Valores de 12 bits de las 16 entradas analógicas lentas.
* **Longitud total del paquete lento:** 14 + 40 + 4 = **58 bytes**.

### Verificación de Integridad (CRC-32)
* Calculado sobre la totalidad del encabezado y la carga útil mediante la función `fdr_calc_crc32()`. Se ubica en los últimos 4 bytes del paquete en formato little-endian.
