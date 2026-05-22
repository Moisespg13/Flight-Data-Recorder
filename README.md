# Flight Data Recorder (FDR) - Firmware

Repositorio del firmware para el **Registrador de Datos de Vuelo (FDR)**, un sistema diseñado para ensayos en vuelo del Centro de Ensayos en Vuelo (CEV-FAA) y desarrollado en el Instituto Universitario Aeronáutico (IUA).

## Descripción General

El sistema FDR está basado en la utilización de dos microcontroladores bajo un entorno de tiempo real (FreeRTOS): un **ESP32-S3** como placa maestra y un **RP2040-Zero** (Waveshare) como placa esclava de adquisición. El hardware está dividido físicamente en dos placas que se comunican mediante un bus SPI dedicado.

El Esclavo adquiere 32 canales analógicos mediante conversores ADC dedicados, empaqueta los datos con timestamps disciplinados por GPS y los transmite al Maestro. El Maestro recibe estos datos, los combina con señales locales (GNSS, IMU, discretas), ensambla frames en el formato estándar CEV y los persiste en una tarjeta MicroSD.

> **Nota sobre el cambio de arquitectura:** Originalmente el sistema empleaba dos ESP32-S3 idénticos. Se decidió reemplazar la placa esclava por una **Raspberry Pi RP2040-Zero** de Waveshare debido a especificaciones físicas del hardware (menor tamaño). Esto implicó dividir el proyecto en tres subcarpetas independientes y portar el firmware del esclavo al Pico SDK con FreeRTOS.

---

## Arquitectura de Hardware

El hardware se particiona funcionalmente en dos tarjetas principales:

### Placa A — MainBoard / Maestra (ESP32-S3)

Encargada del procesamiento principal, almacenamiento, sincronización y comunicaciones. Framework: **ESP-IDF v5.5**.

| Subsistema | Interfaz | Detalle |
|------------|----------|---------|
| Enlace SPI con Esclavo | SPI2 Master (10 MHz) | MOSI=11, MISO=13, CLK=12, CS=10, DRDY=45 |
| MicroSD | SDMMC 1-bit (40 MHz) | CMD=38, CLK=39, D0=40 |
| GNSS | UART2 (9600 baud) | RX=35, TX=36, PPS=37 |
| IMU Xsens | UART0 (115200 baud) | RX=44, TX=43 |
| Puerto de Comandos RS-232 | UART1 (9600 baud) | RX=41, TX=42 (vía MAX3232E) |
| Entradas Discretas | 16 GPIO directos | Lectura digital con pull-down interno |
| Señal de Sincronización | GPIO 9 | SYNC_TRIG → Esclavo (flanco de subida) |
| Detección de Power-Loss | GPIO 46 | Comparador externo (~22V umbral) |
| USB Host (Pendrive) | USB-OTG interno | Descarga de archivos a pendrive |

### Placa B — InputsBoard / Esclava (RP2040-Zero)

Co-procesador dedicado a la adquisición analógica de alta velocidad. Framework: **Pico SDK 2.2 + FreeRTOS SMP**.

| Subsistema | Interfaz | Detalle |
|------------|----------|---------|
| ADC Rápido (16 ch) | SPI0 Master (10 MHz) | 2× AD7606, 16-bit SAR, muestreo simultáneo ≥ 3 kSPS |
| ADC Lento (16 ch) | SPI0 Master (compartido) | 1× AD4131-8, 24-bit Sigma-Delta, ~150 Hz |
| Enlace SPI con Maestro | PIO SPI Slave | GP1=MISO, GP2=SCK, GP0=CS, GP8=DRDY |
| Sincronización | GP14 | SYNC_TRIG ← Maestro |
| Disciplina de Reloj | GP26 | PPS ← GPS (< 4 µs de error) |

> **¿Por qué PIO en lugar de SPI hardware?** El periférico PL022 del RP2040 tiene un bug documentado que provoca desplazamientos de bits en modo esclavo. Se implementó un esclavo SPI por PIO (Programmable I/O) que emula el protocolo bit a bit con sincronismo de fase perfecto.

---

## Arquitectura de Software

El firmware está modularizado y gestionado por tareas concurrentes bajo FreeRTOS en ambas placas para garantizar robustez, tiempos determinísticos y una latencia estricta menor a 2 ms desde la captura hasta el encolado.

### Estructura del Proyecto

Al tratarse de dos plataformas distintas (ESP-IDF y Pico SDK), el proyecto se divide en tres subcarpetas:

```
FDR/
├── FDR_Common/                  # Headers compartidos entre ambas placas
│   ├── protocol.h               #   Protocolo inter-placa (Header + CRC32)
│   └── crc_utils.h              #   Tabla CRC32 (IEEE 802.3)
│
├── FDR_Master/                  # Firmware Maestro (ESP32-S3 / ESP-IDF)
│   ├── main/
│   │   └── FDR_Master.c         #   Punto de entrada, detección de modo
│   └── components/
│       ├── master_config/       #   Configuración centralizada de pines
│       ├── master_logic/        #   Orquestador: inicialización + 8 tareas
│       ├── spi_link/            #   Enlace SPI con el Esclavo (Master)
│       ├── sample_format/       #   Ensamblador de frames CEV (3 formatos)
│       ├── sd_storage/          #   Almacenamiento buffereado en MicroSD
│       ├── gnss/                #   Driver GNSS (NMEA + PPS)
│       ├── imu_xsens/           #   Driver IMU Xsens (protocolo MT)
│       ├── discrete_signals/    #   Lectura de 16 entradas digitales
│       ├── cmd_port/            #   Puerto de comandos RS-232
│       ├── supervisor/          #   Watchdog de software
│       └── usb_transfer/        #   USB Host MSC (copia a pendrive)
│
├── FDR_Slave/                   # Firmware Esclavo (RP2040 / Pico SDK)
│   ├── src/
│   │   ├── main.c               #   Entrada, 5 tareas, coordinator pattern
│   │   ├── fast_signals.c       #   Driver AD7606 ×2 (16 canales rápidos)
│   │   ├── slow_signals.c       #   Driver AD4131 (16 canales lentos)
│   │   ├── spi_link_slave.c     #   Enlace SPI PIO (Slave TX)
│   │   └── spi_slave_tx.pio     #   Máquina de estados PIO (3 instrucciones)
│   ├── include/
│   │   ├── pin_config.h         #   Mapa de pines del RP2040
│   │   ├── fast_signals.h       #   API del AD7606
│   │   ├── slow_signals.h       #   API del AD4131
│   │   ├── spi_link_slave.h     #   API del enlace SPI
│   │   └── FreeRTOSConfig.h     #   Configuración FreeRTOS SMP dual-core
│   └── lib/
│       └── FreeRTOS-Kernel/     #   Kernel FreeRTOS (submodule de Git)
│
├── FDR_Converter/               # Herramienta de post-proceso (Python)
│   ├── fdr_converter.py         #   Conversor .bin → CSV + visualizador
│   └── requirements.txt         #   Dependencias (matplotlib, numpy)
│
└── FDR.code-workspace           # Workspace de VS Code multi-carpeta
```

---

### Componentes del Maestro (ESP32-S3)

| Componente | Descripción |
|------------|-------------|
| `master_config` | Archivo centralizado de asignación de pines (`pin_config.h`) y estructuras del protocolo. |
| `master_logic` | Orquestador principal. Ejecuta la secuencia de inicialización y crea las 8 tareas de vuelo. Contiene la ISR de power-loss. |
| `spi_link` | Recibe paquetes del Esclavo por SPI2. Espera la señal DRDY, ejecuta la transacción, valida Magic Byte + CRC32, y enruta a las colas `q_rx_fast` o `q_rx_slow`. |
| `sample_format` | Ensamblador de frames CEV. Mantiene un buffer compartido de 52 parámetros y aplica tablas de sub-conmutación para construir los frames de 16-bit words según el formato activo (F1, F2 o F3). |
| `sd_storage` | Almacenamiento en MicroSD vía SDMMC. Buffer de escritura de 4 KB, fsync periódico, header de archivo de 12 bytes con autodetección de formato. |
| `gnss` | Driver GNSS por UART. Parsea NMEA ($GPRMC), captura PPS por ISR, mantiene reloj UTC calibrado. Provee fecha/hora al ensamblador CEV. |
| `imu_xsens` | Driver para IMU Xsens MTi por UART. Parsea el protocolo binario MT, extrae 12 parámetros (aceleración, giróscopo, magnetómetro, roll/pitch) y los escala a int16. |
| `discrete_signals` | Lee 16 GPIOs configurados como entrada y los empaqueta en un bitmap de 16 bits. |
| `cmd_port` | Puerto RS-232 para la notebook de mantenimiento. Protocolo de 6 bytes para leer canales y configurar el formato CEV en NVS. |
| `supervisor` | Watchdog de software. Cada tarea hace check-in periódico; si alguna excede su timeout, se registra una alarma. |
| `usb_transfer` | USB Host MSC. Solo activo en Modo Descarga: copia archivos `FDR_*.bin` de la SD a un pendrive. |

### Módulos del Esclavo (RP2040)

| Módulo | Descripción |
|--------|-------------|
| `fast_signals` | Driver para 2× AD7606 (16 canales de 16-bit, muestreo simultáneo a 3 kHz). Usa SPI0 con mutex compartido. Disparo de conversión por pulso CONVST. |
| `slow_signals` | Driver para 1× AD4131-8 (16 canales de 24-bit, ~150 Hz). Comparte SPI0 con el driver rápido. Espera DRDY sin el mutex para minimizar bloqueo. |
| `spi_link_slave` | Empaqueta payloads con header + CRC32 y los transmite por PIO SPI al Maestro, señalizando con DRDY. |
| `spi_slave_tx.pio` | Máquina de estados PIO de 3 instrucciones: espera SCK bajo → emite bit → espera SCK alto. Emula SPI Mode 0 bit-perfect. |
| `time_sync` | Disciplina de reloj PPS. Detecta flancos del pulso de 1 Hz del GPS y corrige el drift del oscilador interno (error < 4 µs). |

---

### Tareas FreeRTOS (14 en total)

#### Maestro — 9 tareas

| Prio | Tarea | Stack | Descripción |
|:----:|-------|------:|-------------|
| 8 | `tsk_frame_writer_master` | 8 KB | Ensambla frames CEV a tasa fija (3000 fr/s en F1/F2, 100 fr/s en F3) y los escribe a la MicroSD. Maneja la señal de power-loss para cierre de emergencia. |
| 7 | `tsk_time_sync_master` | 4 KB | Recibe datos GNSS por UART y gestiona la señal PPS. Alimenta fecha/hora UTC al ensamblador CEV. |
| 6 | `tsk_spi_link_master` | 4 KB | Espera DRDY del Esclavo, ejecuta transacción SPI2, valida paquete y enruta a colas. |
| 5 | `tsk_data_proc_master` | 4 KB | Desencola datos rápidos (ANA 1–16) y actualiza registros globales. |
| 5 | `tsk_slow_proc_master` | 4 KB | Desencola datos lentos (ANA 17–32) y actualiza registros globales. |
| 5 | `tsk_discrete_master` | 2 KB | Lee 16 entradas digitales a 20 Hz y las empaqueta en bitmap de 16 bits. |
| 5 | `tsk_imu_master` | 4 KB | Recibe datos de la IMU Xsens por UART a 100 Hz y los escala a int16. |
| 4 | `tsk_cmd_port_master` | 4 KB | Atiende consultas del protocolo RS-232 (lectura de canal, configuración de formato). |
| 3 | `tsk_supervisor_master` | 2 KB | Watchdog: verifica que todas las tareas hagan check-in dentro de su timeout. |

#### Esclavo — 5 tareas

| Prio | Tarea | Stack | Descripción |
|:----:|-------|------:|-------------|
| 8 | `tsk_coordinator_slave` | 768 B | Espera SYNC_TRIG del Maestro, desbloquea las 4 tareas de trabajo y se autodestruye. |
| 7 | `tsk_acq_fast_slave` | 1 KB | Lee los 2× AD7606 (16 ch) a 3 kHz y envía paquetes a `q_fast_data`. |
| 6 | `tsk_acq_slow_slave` | 1 KB | Lee el AD4131 (16 ch) a ~150 Hz y envía paquetes a `q_slow_data`. |
| 6 | `tsk_spi_link_slave` | 1 KB | Desencola paquetes, agrega header + CRC32 y los transmite por PIO SPI. |
| 6 | `tsk_time_sync_slave` | 768 B | Detecta pulsos PPS y corrige el drift del oscilador (< 4 µs de error). |

---

## Protocolo de Comunicación Inter-placa

Las dos placas se comunican mediante un protocolo binario propietario sobre SPI:

```
┌───────┬──────┬──────────┬──────────┬─────────────────────┬───────┐
│ Magic │ Type │ Sequence │ Timestamp│      Payload        │ CRC32 │
│ 0xFD  │  1B  │   2B     │   8B     │    (variable)       │  4B   │
│  1B   │      │          │          │                     │       │
└───────┴──────┴──────────┴──────────┴─────────────────────┴───────┘
```

| Campo | Tamaño | Descripción |
|-------|-------:|-------------|
| Magic | 1 B | Siempre `0xFD` — permite detectar desalineación |
| Type | 1 B | `0x01` = datos rápidos, `0x02` = datos lentos |
| Sequence | 2 B | Contador rolling para detectar pérdida de paquetes |
| Timestamp | 8 B | Timestamp en nanosegundos, disciplinado por PPS |
| Length | 2 B | Longitud del payload en bytes |
| Payload | variable | `fast_data_payload_t` (40 B) o `slow_data_payload_t` (40 B) |
| CRC32 | 4 B | IEEE 802.3, polinomio `0xEDB88320` |

---

## Formatos de Muestreo CEV

El sistema implementa tres formatos de empaquetamiento conformes al estándar del Centro de Ensayos en Vuelo:

| Formato | Frames/ciclo | Words/frame | Ciclos/seg | Frames/seg | Throughput |
|:-------:|:------------:|:-----------:|:----------:|:----------:|:----------:|
| F1 | 40 | 17 | 75 | 3000 | ~102 KB/s |
| F2 | 40 | 11 | 75 | 3000 | ~66 KB/s |
| F3 | 10 | 6 | 10 | 100 | ~1.2 KB/s |

Cada frame es un bloque de N words de 16 bits (`int16_t`). Los canales rápidos (ANA 1–16) aparecen en cada frame, mientras que los canales lentos, discretas, IMU y tiempo GNSS se distribuyen mediante **tablas de sub-conmutación** a lo largo del ciclo.

El formato activo se almacena en NVS (memoria no volátil) y se puede cambiar remotamente mediante el puerto de comandos RS-232.

---

## Modos de Operación

### Modo Vuelo (sin pendrive al encender)
Modo normal de operación. El sistema completo adquiere, procesa y almacena datos de vuelo continuamente. Si se detecta una caída de tensión en la alimentación de la aeronave (GPIO 46), se ejecuta un cierre de emergencia del archivo para proteger la integridad de los datos.

### Modo Descarga (pendrive conectado al encender)
El sistema no arranca la adquisición. Monta la MicroSD y el pendrive, copia todos los archivos `FDR_*.bin` al pendrive, y queda en idle.

---

## Formato de Archivo de Vuelo

Cada archivo `FDR_XXXX.bin` comienza con un header de 12 bytes que permite la autodetección del formato:

| Offset | Campo | Tipo | Valor |
|:------:|-------|------|-------|
| 0–3 | `magic` | uint32 | `0xFD464452` ("FDR" + 0xFD) |
| 4 | `version` | uint8 | `1` |
| 5 | `format` | uint8 | `1`, `2` o `3` |
| 6–7 | `reserved` | uint16 | `0x0000` |
| 8–11 | `crc32` | uint32 | CRC32 de los bytes [0..7] |

A continuación del header se escriben los frames CEV como bloques consecutivos de `int16_t`, sin separadores ni headers intermedios.

---

## Herramienta de Post-proceso (FDR Converter)

El repositorio incluye una herramienta de escritorio en Python (`FDR_Converter/`) para el análisis post-vuelo:

- **Autodetección** del formato CEV mediante el header del archivo.
- **Decodificación** de la sub-conmutación (des-multiplexación de los 52 parámetros).
- **Exportación a CSV** con una columna por parámetro y una fila por ciclo.
- **Visualización interactiva** con gráficos de señales analógicas, discretas, IMU y timestamps GNSS.
- **Detección automática de pendrive** para uso offline en el hangar.

```bash
pip install matplotlib numpy
python fdr_converter.py
```

---

## Flujo de Datos Completo

```
         RP2040 (Esclavo)                         ESP32-S3 (Maestro)
┌─────────────────────────┐              ┌──────────────────────────────────┐
│                         │              │                                  │
│ AD7606 ×2 ──► acq_fast ─┤              │  GPS ──UART──► gnss ──────────┐  │
│ (16 ch, 3kHz)           │              │  IMU ──UART──► imu_xsens ───┐ │  │
│                         │              │  16 GPIO ────► discrete ──┐ │ │  │
│ AD4131 ────► acq_slow ──┤   SPI+DRDY   │                          │  │ │  │
│ (16 ch, 150Hz)          ├─────────────►│  spi_link ──► data_proc──┤  │ │  │
│                         │              │              slow_proc ──┤  │ │  │
│ PPS ───────► time_sync  │              │                          ▼  ▼ ▼  │
│ (< 4µs drift)           │              │              ┌──────────────────┐│
│                         │              │              │ sample_format    ││
│ SYNC_TRIG ─► coordinator│              │              │ (ensamblador CEV)││
│                         │              │              └────────┬─────────┘│
└─────────────────────────┘              │                       ▼          │
                                         │              frame_writer ──► SD │
                                         │              (FDR_XXXX.bin)      │
                                         └──────────────────────────────────┘
```

---

## Requisitos y Configuración de Desarrollo

### Placa Maestra (ESP32-S3)
- **Framework:** Espressif IoT Development Framework (ESP-IDF v5.5).
- **Lenguaje:** C.
- **IDE Recomendado:** Visual Studio Code con la extensión [ESP-IDF](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension).

### Placa Esclava (RP2040-Zero)
- **Framework:** Raspberry Pi Pico SDK 2.2 + FreeRTOS SMP (submódulo de Git).
- **Lenguaje:** C.
- **IDE Recomendado:** Visual Studio Code con la extensión [Raspberry Pi Pico](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico).

### Post-proceso (FDR Converter)
- **Lenguaje:** Python 3.11+.
- **Dependencias:** `matplotlib`, `numpy`, `tkinter` (incluido en Python estándar).

### Compilación

```bash
# Maestro (ESP-IDF)
cd FDR_Master
idf.py build
idf.py flash monitor

# Esclavo (Pico SDK)
cd FDR_Slave
mkdir build && cd build
cmake ..
make
# Copiar .uf2 al RP2040 en modo BOOTSEL
```
