# FDR_Master — ESP32-S3

Firmware de la placa maestra del registrador de vuelo (FDR), implementado sobre **ESP-IDF v5.5** para el SoC **ESP32-S3** (módulo ESP32-S3-WROOM-1-N16R8, con 16 MB de memoria Flash Quad-SPI y 8 MB de memoria PSRAM Octal-SPI).

La placa maestra concentra los datos provenientes del subsistema esclavo (RP2040), el receptor GNSS, el RTC DS3232, la unidad inercial (IMU Xsens) y el módulo de 16 entradas discretas (TCA9555), conformando las tramas según la norma de sub-conmutación del CEV (Formatos 1, 2 o 3) y almacenándolas simultáneamente en dos tarjetas MicroSD redundantes con sistema de archivos FAT32.

---

## Modos de Operación

Al encenderse el equipo, el sistema evalúa la presencia de dispositivos en el bus USB nativo (GPIO 19 y 20):
1. **Modo Descarga (USB Host MSC):** Si se detecta un pendrive conectado durante la rutina de inicio, el firmware no arranca la adquisición de vuelo; monta las tarjetas MicroSD en modo de solo lectura y copia automáticamente todos los archivos `FDR_*.bin` al pendrive. Al finalizar la transferencia, desmonta los volúmenes de forma segura.
2. **Modo Vuelo (Grabación Continua):** Si no hay pendrive presente, se inicializan todos los sensores, buses y tareas de grabación en las tarjetas MicroSD.

---

## Arquitectura de Memoria y Buffer en PSRAM

El SoC utiliza **8 MB de PSRAM Octal-SPI a 40 MHz** configurada en `sdkconfig.defaults`. Los pines GPIO 33 a 37 quedan reservados exclusivamente por hardware para el bus octal de esta memoria.

* **Buffer Circular de Grabación (6 MB):** Se reserva en PSRAM mediante `heap_caps_aligned_alloc` con alineación de 32 bytes. A una tasa máxima de generación de datos de ~522 KB/s (Formato 1 a 3000 frames/s), este búfer proporciona una **capacidad de absorción de ~11,5 segundos** de retención continua frente a latencias prolongadas de escritura o reorganización interna de bloques flash de las tarjetas SD.
* **SRAM Interna (512 KB):** Reservada para pilas de tareas de FreeRTOS, descriptores DMA de periféricos y búferes de comunicación críticos.

---

## Tabla de Tareas del Sistema (FreeRTOS)

| Tarea | Prioridad | Núcleo | Descripción |
|-------|:---------:|:------:|-------------|
| `tsk_spi_link` | 12 | 1 | Recepción del enlace SPI2 esclavo disparada por interrupción de DRDY (GPIO 11). |
| `tsk_assemble_cev` | 9 | 1 | Ensamblado de tramas CEV a partir de parámetros y copiado atómico al anillo PSRAM. |
| `tsk_time_sync` | 7 | Asignado | Decodificación NMEA (UART2) y captura de pulsos PPS (GPIO 15). |
| `tsk_sd_io` | 6 | 0 | Extracción de bloques de 32 KB del anillo y escritura simultánea en SD1 y SD2. |
| `tsk_proc_fast` | 5 | 1 | Desempaquetado de muestras analógicas rápidas (3 kHz) del RP2040. |
| `tsk_proc_slow` | 5 | 1 | Desempaquetado de muestras analógicas lentas (20 Hz) del RP2040. |
| `tsk_imu_master` | 5 | Asignado | Lectura y parseo del protocolo binario de la IMU Xsens MTi (UART0). |
| `tsk_discrete_master`| 5 | Asignado | Lectura y filtrado antirrebote de 16 discretas vía TCA9555 por I2C0 (20 Hz). |
| `tsk_cmd_port` | 4 | Asignado | Procesamiento de comandos de configuración y mantenimiento RS-232 (UART1). |
| `tsk_supervisor` | 3 | Asignado | Verificación periódica de check-in de tareas de software. |
| `tsk_led` | 1 | 0 | Indicación visual del estado del sistema mediante secuencias en el LED testigo (GPIO 0). |

---

## Subsistema de Grabación Redundante (MicroSD)

* **Bus SPI3 a 20 MHz:** Líneas compartidas MOSI (GPIO 38), CLK (GPIO 39) y MISO (GPIO 40). Selección de chip independiente para SD1 (GPIO 1) y SD2 (GPIO 2).
* **Escritura por bloques de 32 KB:** La tarea `sd_io` extrae datos del anillo en bloques de 32 KB (`WRITE_CHUNK_SIZE`), igualando el tamaño de clúster estándar de particiones FAT32 para evitar operaciones de lectura-modificación-escritura en las tarjetas.
* **Aislamiento automático de fallas:** Si una tarjeta falla, se desconecta o supera los tiempos de respuesta, las banderas `sd1_failed` o `sd2_failed` desactivan la tarjeta afectada. La grabación continúa ininterrumpidamente en la tarjeta operativa sin retardo de bus.
* **Sincronización desfasada (`fsync`):** Para minimizar el impacto de las operaciones de sincronización de metadatos FAT32, se ejecuta un `fsync` alternado: SD1 en minutos pares (2, 4, 6...) y SD2 en minutos impares (3, 5, 7...). Si el búfer circular supera el 50% de ocupación, el `fsync` se pospone 60 segundos automáticamente.
* **Detección de corte de energía (Power-Loss):** Una interrupción en GPIO 46 detecta la caída de la alimentación principal, señalizando al sistema para volcar de inmediato los datos residuales del anillo, forzar el `fsync` y cerrar ambos archivos de vuelo de manera ordenada.

---

## Mapa de Pines GPIO

| Función | Señal | GPIO | Descripción / Conexión |
|---------|-------|:----:|------------------------|
| **Enlace SPI2 (RP2040)** | CS | 10 | Salida Chip Select hacia RP2040 (GP0) |
| | DRDY | 11 | Entrada Data Ready desde RP2040 (GP3) |
| | SCK | 12 | Salida Clock hacia RP2040 (GP2) |
| | MISO | 13 | Entrada datos desde RP2040 (GP1) |
| **MicroSD (SPI3)** | CS1 | 1 | Chip Select MicroSD #1 |
| | CS2 | 2 | Chip Select MicroSD #2 |
| | MOSI | 38 | Master Out Slave In compartido |
| | SCK | 39 | Reloj SPI compartido (20 MHz) |
| | MISO | 40 | Master In Slave Out compartido |
| **GNSS** | PPS | 15 | Entrada de pulso PPS (1 Hz) |
| | TX | 16 | Salida de comandos al módulo GNSS |
| | RX | 17 | Entrada NMEA (9600 baudios) |
| **IMU Xsens** | TX | 43 | Salida de comandos a la IMU |
| | RX | 44 | Entrada de telemetría IMU (115200 baudios) |
| **Comandos RS-232** | RX | 41 | Entrada desde transceptor MAX3232 |
| | TX | 42 | Salida hacia transceptor MAX3232 |
| **Discretas y RTC (I2C0)** | SCL | 4 | Reloj I2C hacia TCA9555 (0x20) y RTC DS3232 (0x68) (400 kHz) |
| | SDA | 5 | Datos I2C hacia TCA9555 y RTC DS3232 |
| **Control / Sistema** | LED | 0 | LED indicador de estado (activo en bajo) |
| | PWR_LOST | 46 | Entrada de detección de falla de alimentación |
| | USB D- | 19 | USB Host MSC Data - |
| | USB D+ | 20 | USB Host MSC Data + |
| **PSRAM Octal** | D0..D7, CLK, CS | 33–37 | Bus interno reservado para memoria PSRAM |

---

## Compilación y Grabación

Requisitos: **ESP-IDF v5.5** configurado en el entorno de desarrollo.

```powershell
cd FDR_Master

# Configuración del proyecto (aplica sdkconfig.defaults)
idf.py reconfigure

# Compilación
idf.py build

# Grabación y apertura de monitor serie
idf.py -p COMx flash monitor
```

Los registros de diagnóstico y depuración del sistema se emiten a través del puerto nativo USB-Serial-JTAG del ESP32-S3.
