# Flight Data Recorder (FDR) - Firmware

Firmware del registrador de datos de vuelo (FDR) para ensayos del Centro de Ensayos en Vuelo (CEV-FAA), desarrollado en el Instituto Universitario Aeronáutico (IUA).

El detalle completo de arquitectura, pines, protocolo y formatos se encuentra documentado en [`DocumentacionDeContexto/fdr_system_architecture.md`](DocumentacionDeContexto/fdr_system_architecture.md).

---

## Descripción General

El registrador opera mediante una arquitectura distribuida de dos microcontroladores concurrentes bajo FreeRTOS:

- **Placa Maestra (ESP32-S3)**: Programada sobre el framework ESP-IDF (v5.5). Recibe las tramas de adquisición del esclavo vía SPI, sincroniza el tiempo absoluto con el receptor GNSS (UART2 + PPS), captura datos inerciales de la IMU Xsens MTi (UART0) y 16 señales discretas desde un expansor I2C **TCA9555** a 400 kHz. Ensambla las tramas según los estándares CEV (Formatos 1, 2 o 3) y las almacena con redundancia en dos tarjetas MicroSD (SPI3) mediante un búfer anular de 6 MB alojado en memoria PSRAM octal.
- **Placa Esclava (RP2040-Zero)**: Programada con Raspberry Pi Pico SDK 2.2 y FreeRTOS SMP. Muestrea 16 canales analógicos rápidos a 3 kHz mediante 2× AD7606 (16 bits, muestreo simultáneo) y 16 canales lentos a ~20 Hz con 1× AD7490 (12 bits) sobre el bus SPI0. Transmite los datos al maestro mediante un esclavo SPI implementado en las máquinas de estados programables (**PIO**) del RP2040 con soporte DMA, eliminando los problemas de corrimiento de bits del controlador SPI por hardware integrado.

### Sincronización y Modos de Operación

- **Arranque Sincrónico por CS**: La adquisición arranca de forma sincrónica con el primer flanco descendente de *Chip Select* (CS) del bus SPI inter-placa. Se eliminó la línea física previa `SYNC_TRIG`, por lo que el encendido de ambas placas es completamente indistinto.
- **Detección Automática de Modo**:
  - **Modo Descarga (USB Host MSC)**: Si al energizar el equipo se detecta un pendrive en el puerto USB nativo (GPIO 19/20), el sistema cancela las tareas de adquisición y copia automáticamente todos los archivos de vuelo `FDR_*.bin` al pendrive en bloques de 8 KB.
  - **Modo Vuelo**: Si no hay pendrive, inicia la orquestación y grabación redundante continua en ambas MicroSD.
- **Protección ante Corte de Energía (Power-Loss)**: Un comparador por hardware genera una interrupción de alta prioridad en GPIO 46 ante caídas de la alimentación principal de la aeronave (~22 V), forzando el vaciado del remanente y el cierre seguro de los archivos FAT32 antes de que se agote la energía de los capacitores de respaldo.

---

## Características Principales y Mejoras Recientes

- **Búfer Anular de 6 MB en PSRAM Octal**: Se expandió el búfer circular en memoria PSRAM octal (8 MB a 40 MHz) de 2 MB a 6 MB. Esto permite amortiguar con holgura los bloqueos periódicos de borrado físico interno (*Erase Blocks* NAND de 1 MB) característicos de las tarjetas MicroSD durante escrituras continuas de alto caudal (~100 KB/s en Formato 1).
- **Formateo con Clústeres de 32 KB y Búfer de Escritura Alineado**: Las tarjetas MicroSD se formatean en FAT32 con clústeres de 32 KB, y el búfer de volcado de la tarea escritora se configuró a 32 KB para coincidir exactamente con el tamaño de clúster. Esto minimiza las actualizaciones parciales de la FAT y optimiza la velocidad de escritura de las tarjetas.
- **Tolerancia a Fallos y Aislamiento Automático de MicroSD**: Si una de las dos tarjetas MicroSD presenta una falla de bus, bloqueo o error de escritura (`fwrite`), el controlador dual la desactiva automáticamente y continúa registrando sin interrupciones en la tarjeta operativa restante.
- **Cadencia de Sincronización (`fsync`) Desfasada**: El vaciado de metadatos al disco se realiza de manera periódica pero alternada en el tiempo (SD1 a los 5 minutos, SD2 a los 7,5 minutos), evitando que ambas tarjetas bloqueen simultáneamente el bus SPI3 compartido.
- **Validación Prolongada de más de 3 Horas**: Sistema validado con ensayos ininterrumpidos de más de 3 horas en el banco de pruebas con señales en rampa simuladas, verificando la ausencia total de pérdidas de muestras (*0 drops*) y la integridad estructural de los datos.
- **FDR Converter con FFT, LOD Dinámico y Ejecutable**:
  - Incorporación de análisis en frecuencia mediante **FFT** para visualización espectral y detección armónica.
  - Implementación de **Nivel de Detalle Dinámico (LOD)**: sub-muestreo inteligente para la renderización gráfica que ajusta los puntos dibujados en tiempo real según el nivel de zoom, permitiendo explorar grabaciones masivas (más de 1 h o 3 h con millones de muestras) con total fluidez y bajo consumo de memoria RAM.
  - Generación de ejecutable independiente para Windows en `FDR_Converter/dist/FDR_Converter.exe`.

---

## Mapa de Pines y Conectividad

### Placa Maestra (ESP32-S3-DevKitC-1)

| Periférico / Función | Interfaz | Pines GPIO |
|----------------------|----------|------------|
| Enlace con Esclavo | SPI2 Master (10 MHz) | MISO 13, CLK 12, CS 10, DRDY 11 *(1)* |
| Almacenamiento Dual MicroSD | SPI3 (20 MHz, bus compartido) | MOSI 38, CLK 39, MISO 40, CS1 1, CS2 2 |
| Receptor GNSS (NMEA + PPS) | UART2 (9600 baud) + GPIO | RX 17, TX 16, PPS 15 *(2)* |
| IMU Xsens MTi | UART0 (115200 baud) | RX 44, TX 43 |
| Puerto de Comandos | UART1 (115200 baud, MAX3232) | RX 41, TX 42 |
| Expansor 16 Discretas | I2C (400 kHz, TCA9555 `0x20`) | SCL 4, SDA 5 |
| Detección de Power-Loss | GPIO (flanco de bajada) | GPIO 46 |
| LED de Estado | GPIO | GPIO 0 |
| Descarga USB Host | USB OTG nativo (MSC) | D- GPIO 19, D+ GPIO 20 |
| Bus de PSRAM Octal | Interfaz interna octal | GPIO 33 a 37 *(reservados por hardware)* |
| Pines Libres | Disponibles | GPIO 3, 6, 7, 8, 9, 14, 18, 21, 45, 47, 48 |

> *(1) DRDY fue remapeado a GPIO 11 (aprovechando la línea MOSI libre, que no se utilizaba al ser un canal unidireccional de recepción) para evitar usar GPIO 45, que es un pin de strapping crítico para el arranque del SoC.*  
> *(2) El GNSS se trasladó a GPIO 15, 16 y 17 para liberar GPIO 33–37 para la memoria PSRAM octal.*

### Placa Esclava (RP2040-Zero)

| Periférico / Función | Interfaz | Pines GP |
|----------------------|----------|----------|
| Enlace con Maestro | PIO SPI Slave (10 MHz, DMA) | CS GP0, MISO GP1, SCK GP2, DRDY GP3 *(3)* |
| AD7606 ×2 (16 canales rápidos) | SPI0 Master (10 MHz, modo 2) | MISO GP4, SCK GP6, MOSI GP7, CS0 GP5, CS1 GP27, CONVST GP10, BUSY0 GP12, BUSY1 GP28, RST GP15 |
| AD7490 (16 canales lentos) | SPI0 Master (10 MHz, modo 3) | CS2 GP9 *(4)* |
| Sincronización GNSS PPS | GPIO (flanco de subida) | GP26 |
| Pulsador Stand-By | GPIO | GP13 |
| LED Integrado | WS2812 NeoPixel | GP16 |
| Pines Libres | Disponibles | GP8, GP11, GP14, GP29 |

> *(3) DRDY se reubicó en GP3 (ocupando la línea MOSI libre), dejando desocupado GP8.*  
> *(4) Al adoptarse el AD7490 (conversión comandada por CS), se prescindió del pin DRDY del AD4131 previo en GP29, quedando libre.*

---

## Formatos de Telemetría CEV

| Formato | Frames/ciclo | Palabras/frame | Frecuencia de tramas | Descripción |
|:-------:|:------------:|:--------------:|:--------------------:|:------------|
| **F1** | 40 | 17 | 3000 Hz | Ensayos rápidos (alta resolución temporal) |
| **F2** | 40 | 11 | 3000 Hz | Ensayos intermedios |
| **F3** | 10 | 6 | 100 Hz | Ensayos de crucero / baja tasa |

- **Cabecera binaria `FDR_XXXX.bin` (12 bytes)**: Magic `0xFD464452` ("FDR"), número de versión (uint16), formato CEV activo (uint16) y CRC32 de los 8 bytes iniciales.
- **Sincronismo temporal**: El pulso PPS del GNSS se mapea en el parámetro **DSP 12**, y el ángulo de Yaw de la IMU en **DSP 13**.
- **Gestión Remota**: El formato activo se almacena en memoria no volátil (NVS) y puede consultarse o reprogramarse remotamente desde el puerto RS-232 o la interfaz del *FDR Converter*.

---

## Estructura del Repositorio

```
FDR/
├── FDR_Common/          # protocol.h, crc_utils.h (estructuras de paquetes y CRC32)
├── FDR_Master/          # Firmware ESP32-S3 (ESP-IDF v5.5)
│   ├── components/      # master_logic, spi_link, sd_storage, sample_format,
│   │                    # gnss, imu_xsens, discrete_signals, cmd_port,
│   │                    # supervisor, usb_transfer, master_config
│   └── main/            # Punto de entrada de la aplicación
├── FDR_Slave/           # Firmware RP2040-Zero (Pico SDK 2.2 + FreeRTOS SMP)
│   ├── include/         # pin_config.h, fast_signals.h, slow_signals.h
│   └── src/             # main.c, spi_slave_tx.pio, drivers AD7606 y AD7490
├── FDR_Converter/       # Herramienta de visualización y post-procesamiento (Python/Tkinter)
│   ├── fdr_converter.py # Aplicación gráfica con FFT, calibración y LOD dinámico
│   ├── requirements.txt # Dependencias (matplotlib, numpy, scipy)
│   └── dist/            # Ejecutable generado FDR_Converter.exe
└── DocumentacionDeContexto/
```

---

## Compilación y Despliegue

### 1. Firmware Maestro (ESP32-S3)
Requiere el entorno **ESP-IDF v5.5** instalado y configurado:
```bash
cd FDR_Master
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

### 2. Firmware Esclavo (RP2040-Zero)
Requiere **Pico SDK 2.2**, **CMake** y **Ninja/Make**:
```bash
cd FDR_Slave
mkdir build && cd build
cmake .. -G "Ninja"
ninja
# Copiar el archivo binario FDR_Slave.uf2 al RP2040 montado en modo BOOTSEL
```

### 3. FDR Converter (PC)
```bash
cd FDR_Converter
pip install -r requirements.txt
python fdr_converter.py
```
*O bien ejecutar directamente el archivo independiente precompilado en `FDR_Converter/dist/FDR_Converter.exe`.*
