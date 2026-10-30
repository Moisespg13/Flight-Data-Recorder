# spi_link — Enlace de Comunicación Inter-Placa (Maestro)

Componente que implementa el lado maestro de la interfaz de comunicación SPI de alta velocidad entre el SoC ESP32-S3 (Placa Maestra) y el microcontrolador RP2040 (Placa Esclava), sobre el periférico hardware SPI2 a una frecuencia de reloj de 10 MHz.

---

## Interfaz de Hardware y Señales

| Señal | GPIO ESP32-S3 | Pin RP2040 | Dirección | Función |
|-------|:-------------:|:----------:|:---------:|---------|
| **CS** | 10 | GP0 | Master $\rightarrow$ Slave | Selección de chip (activo en bajo). Inicia la transferencia. |
| **DRDY** | 11 | GP3 | Slave $\rightarrow$ Master | Data Ready. El esclavo notifica que tiene un paquete preparado en FIFO. |
| **SCK** | 12 | GP2 | Master $\rightarrow$ Slave | Señal de reloj serie síncrono a 10 MHz. |
| **MISO** | 13 | GP1 | Slave $\rightarrow$ Master | Línea de datos serie unidireccional hacia la maestra. |

> **Nota de Hardware:** En el diseño definitivo, la línea DRDY se asignó al GPIO 11, evitando el uso del terminal GPIO 45 (pin de strapping de arranque del ESP32-S3). Asimismo, se eliminó la línea física dedicada `SYNC_TRIG` (originalmente en GPIO 9); el arranque y despertar del esclavo se coordina directamente a través del primer flanco de bajada de la línea CS.

---

## Protocolo de Handshake por Interrupción

1. **Notificación de Datos:** Cuando el microcontrolador RP2040 completa el empaquetado de muestras analógicas en su búfer de transmisión DMA, eleva la línea **DRDY a nivel alto**.
2. **Atención por Interrupción (ISR):** Un flanco ascendente en el GPIO 11 dispara la interrupción en el ESP32-S3, desbloqueando de forma inmediata a la tarea de alta prioridad `tsk_spi_link` (Prioridad 12, alojada en el Núcleo 1).
3. **Lectura por Ráfaga SPI:** La tarea baja la línea CS (GPIO 10) y emite los pulsos de reloj a 10 MHz para clockear la totalidad del paquete entrante hacia la memoria SRAM interna del ESP32-S3.
4. **Finalización de Trama:** Al detectar el flanco de subida de CS al término del reloj, el RP2040 baja la señal DRDY a nivel cero, quedando listo para conformar el siguiente lote de muestras.

---

## Estructura de Paquetes y Desempaquetado

El enlace intercambia paquetes binarios protegidos por una suma de verificación por redundancia cíclica (CRC32):

* **Paquete Rápido (`PKT_TYPE_FAST_DATA` - 178 bytes):**
  * Encabezado fijo de 14 bytes (Magic `0xFD`, tipo, número de secuencia, timestamp base en nanosegundos, longitud de datos).
  * Lote de **4 muestras analógicas consecutivas** (40 bytes cada una, total 160 bytes). Cada una de las 4 muestras contiene sus 16 canales de 16 bits y su respectivo timestamp calibrado independiente de 64 bits.
  * Suma de verificación CRC32 de 4 bytes.
  * Este empaquetado reduce la tasa de interrupciones del enlace a solo 750 paquetes por segundo (frente a los 3000 Hz del muestreo bruto), eliminando cuellos de botella en la CPU.
* **Paquete Lento (`PKT_TYPE_SLOW_DATA` - 58 bytes):**
  * Encabezado de 14 bytes, 1 muestra analógica lenta de 16 canales (40 bytes) y CRC32 de 4 bytes. Transmitido a una cadencia de 20 Hz.

### Validación y Enrutamiento
Al recibirse cada paquete, la tarea valida el byte de preámbulo `0xFD` y calcula el CRC32 sobre los datos recibidos. Si el paquete es íntegro, extrae las muestras individuales y las deposita en las colas de procesamiento `q_fast_rx` o `q_slow_rx` para su posterior ensamble en la trama CEV.
