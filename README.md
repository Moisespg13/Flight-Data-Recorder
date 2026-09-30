# Flight Data Recorder (FDR) - Firmware

Firmware del registrador de datos de vuelo (FDR) para ensayos del Centro de Ensayos en Vuelo (CEV-FAA), desarrollado en el Instituto Universitario Aeronáutico (IUA).

El detalle de pines, protocolo, tareas y formatos está en [`DocumentacionDeContexto/fdr_system_architecture.md`](DocumentacionDeContexto/fdr_system_architecture.md).

## Descripción

Dos microcontroladores bajo FreeRTOS:

- **Maestro, ESP32-S3** (ESP-IDF v5.5). Recibe los analógicos del esclavo, los junta con GNSS, IMU Xsens y 16 discretas (expansor I2C **TCA9555**), arma tramas CEV (formatos 1, 2 o 3) y las graba en dos MicroSD.
- **Esclavo, RP2040-Zero** (Pico SDK 2.2 + FreeRTOS SMP). Muestrea 16 canales rápidos con 2× AD7606 a 3 kHz y 16 lentos con 1× AD7490 a ~20 Hz. Los transmite por un esclavo SPI hecho en PIO: el SPI de hardware del RP2040 desplaza bits en modo esclavo.

El esclavo arranca a muestrear con el primer flanco de bajada de CS del enlace. No hay cable `SYNC_TRIG`.

Al encender, si hay un pendrive el maestro copia `FDR_*.bin` (modo descarga, USB Host MSC). Si no hay pendrive, entra en modo vuelo. Un flanco de bajada en GPIO 46 cierra el archivo ante una caída de alimentación.

## Dos árboles

| Carpeta | Para qué es |
|---------|-------------|
| `FDR/` (este árbol) | Firmware de la PCB |
| `FDR_TB/` | Protoboard: 2 módulos de SD y señales simuladas. Es el firmware más nuevo |

`FDR_TB` ya comparte con este árbol el paquete rápido de **4** muestras y el `fsync` desfasado (SD1 a los 2, 4 y 6 minutos; SD2 a los 3, 5 y 7).

Lo que ya está en los dos: PSRAM octal de 8 MB a 40 MHz, anillo de grabación de 2 MB en esa memoria, GNSS en GPIO 17/16/15 (los GPIO 33–37 los usa la PSRAM) y el ensamblado CEV en el núcleo 1, separado de la escritura a SD en el núcleo 0. La protoboard no lee el TCA9555 porque ese integrado no está en el banco: la tarea de discretas inyecta un patrón simulado. El diseño de las discretas sigue siendo el TCA9555 en el maestro.

## Hardware de esta PCB

| Subsistema | Interfaz | Pines |
|------------|----------|-------|
| Enlace con el esclavo | SPI2 master, 10 MHz, DRDY por interrupción | MISO 13, CLK 12, CS 10, DRDY 11 |
| MicroSD ×2 | SPI3, 20 MHz, MISO compartido | MOSI 38, CLK 39, MISO 40, CS1 1, CS2 2 |
| GNSS | UART2, 9600 baud, NMEA. PSRAM octal en GPIO 33–37 | RX 17, TX 16, PPS 15 |
| IMU Xsens | UART0, 115200 baud | RX 44, TX 43 |
| Comandos | UART1, 115200 baud, MAX3232 | RX 41, TX 42 |
| Discretas | I2C TCA9555 `0x20`, 400 kHz, debounce de 3 muestras | SCL 4, SDA 5 |
| Power-loss | GPIO, flanco de bajada | 46 |
| USB Host | OTG, pendrive MSC | GPIO 19 / 20 |

### Esclavo

| Subsistema | Interfaz | Pines |
|------------|----------|-------|
| AD7606 ×2 | SPI0 master, modo 2, mutex compartido | MISO GP4, SCK GP6, MOSI GP7, CS GP5 y GP27, CONVST GP10, BUSY GP12 y GP28, RST GP15 |
| AD7490 | SPI0, modo 3 (el driver cambia el modo del bus y lo restaura) | CS GP9 |
| Enlace PIO | SPI modo 0, DMA | CS GP0, MISO GP1, SCK GP2, DRDY GP3 |
| PPS | GPIO, flanco de subida | GP26 |

## Tareas

Maestro: `tsk_spi_link_master` (prioridad 12, núcleo 1), ensamblado CEV (9, núcleo 1), escritura a SD (6, núcleo 0), time sync (7), procesado rápido y lento (5, núcleo 1), discretas, IMU (5), comandos (4), supervisor (3). El anillo de 2 MB está en la PSRAM; las pilas de las tareas quedan en la SRAM interna.

Esclavo (6): coordinador (8, se destruye al ver el primer CS), adquisición rápida (7, timer de 333 µs), lenta (6, 50 ms), enlace SPI (6), time sync (6), watchdog de hardware (1, timeout 2 s).

## Protocolo inter-placa

Cabecera: magic `0xFD`, tipo, secuencia (2 B), timestamp en ns (8 B), largo (2 B). Después el payload y un CRC32 (IEEE 802.3). Tipo `0x01` = rápidos, `0x02` = lentos.

Cada paquete rápido lleva cuatro muestras (178 bytes). La cabecera usa el timestamp de la primera y cada muestra conserva el suyo. El paquete lento sigue siendo una muestra (58 bytes).

## Formatos CEV

| Formato | Frames/ciclo | Words/frame | Frames/s |
|:-------:|:------------:|:-----------:|:--------:|
| F1 | 40 | 17 | 3000 |
| F2 | 40 | 11 | 3000 |
| F3 | 10 | 6 | 100 |

El yaw de la IMU se guarda como **DSP 13** (una celda que en la planilla CEV estaba libre). DSP 12 es el sincronismo PPS. El formato activo vive en NVS y se cambia por el puerto de comandos (mensajes de 6 bytes, header `0xAA55`; el formateo de SD pide la clave `0x55AA`). El FDR Converter puede leerlo y guardarlo; el valor se aplica al reiniciar.

Cada `FDR_XXXX.bin` abre con 12 bytes: magic `0xFD464452`, versión, formato y CRC32 de esos primeros 8 bytes. Siguen las tramas `int16` sin separadores.

## Estructura

```
FDR/
??? FDR_Common/            # protocol.h, crc_utils.h
??? FDR_Master/            # ESP-IDF
?   ??? components/        # master_logic, spi_link, sample_format, sd_storage,
?                          # gnss, imu_xsens, discrete_signals, cmd_port,
?                          # supervisor, usb_transfer, master_config
??? FDR_Slave/             # Pico SDK
?   ??? src/               # main.c, fast_signals.c, slow_signals.c, spi_link_slave.c
??? FDR_Converter/         # .bin ? CSV + gráficos
??? DocumentacionDeContexto/
```

## Compilación

```bash
# Maestro
cd FDR_Master
idf.py build
idf.py flash monitor

# Esclavo
cd FDR_Slave
mkdir build && cd build
cmake ..
ninja
# Copiar build/FDR_Slave.uf2 al RP2040 en BOOTSEL
```

```bash
# Post-proceso
pip install matplotlib numpy
python FDR_Converter/fdr_converter.py
```
