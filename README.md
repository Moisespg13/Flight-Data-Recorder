# Flight Data Recorder (FDR) - Firmware

Repositorio del firmware para el Registrador de Datos de Vuelo (FDR), un sistema diseñado para ensayos en vuelo del Centro de Ensayos en Vuelo (CEV-FAA) y desarrollado en el Instituto Universitario Aeronáutico (IUA).

## Descripción General
El sistema FDR está basado en la utilización de dos microcontroladores bajo un entorno de tiempo real (FreeRTOS): un **ESP32-S3** como placa maestra y un **RP2040-Zero** (Waveshare) como placa esclava de adquisición. El hardware está dividido físicamente en dos placas que se comunican mediante un bus SPI.

> **Nota sobre el cambio de arquitectura:** Originalmente el sistema empleaba dos ESP32-S3 idénticos. Se decidió reemplazar la placa esclava por una **Raspberry Pi RP2040-Zero** de Waveshare debido a especificaciones físicas del hardware (menor tamaño). Esto implicó dividir el proyecto en tres subcarpetas independientes y portar el firmware del esclavo al Pico SDK con FreeRTOS.

## Arquitectura de Hardware
El hardware se particiona funcionalmente en dos tarjetas principales:

*   **Placa A (MainBoard / Maestra — ESP32-S3):** Encargada del procesamiento principal, almacenamiento masivo y redundante (microSD y eMMC), sincronización absoluta vía GNSS y RTC (DS3232), descarga de datos vía USB-CDC y **lectura de 16 entradas discretas** (GPIO directo). Framework: **ESP-IDF**.
*   **Placa B (InputsBoard / Adquisición / Esclava — RP2040-Zero):** Actúa como co-procesador dedicado a adquirir y acondicionar las señales analógicas provenientes de la aeronave. Framework: **Pico SDK + FreeRTOS**.
    *   *Entradas Analógicas Rápidas:* 16 canales mediante 2 conversores AD7606 de 16 bits (muestreo simultáneo a $\ge 3$ kSPS).
    *   *Entradas Analógicas Lentas:* 16 canales mediante 1 conversor de ultra bajo consumo AD4131-8 (frecuencia de hasta 10 SPS).

## Arquitectura de Software
El firmware está modularizado y gestionado por tareas concurrentes bajo FreeRTOS en ambas placas para garantizar la robustez, tiempos determinísticos y una latencia estricta menor a 2 ms desde la captura hasta el encolado.

### Estructura del Proyecto
Al tratarse de dos plataformas distintas (ESP-IDF y Pico SDK), el proyecto se divide en tres subcarpetas:

```
FDR/
├── FDR_Common/          # Headers compartidos entre ambas placas
│   ├── protocol.h       # Protocolo de comunicación inter-placa (TLV + CRC)
│   └── crc_utils.h      # Utilidades de CRC32
├── FDR_Master/          # Firmware Maestro (ESP32-S3 / ESP-IDF)
│   ├── main/
│   │   └── FDR_Master.c
│   └── components/
│       ├── discrete_signals/   # Lectura de 16 entradas digitales (GPIO)
│       ├── master_config/      # Configuración de pines del Maestro
│       ├── master_logic/       # Lógica principal: colas, disparo SYNC_TRIG
│       └── spi_link/           # Enlace SPI con el Esclavo (modo Maestro)
├── FDR_Slave/           # Firmware Esclavo (RP2040-Zero / Pico SDK + FreeRTOS)
│   ├── src/
│   │   ├── main.c             # Punto de entrada, creación de tareas
│   │   ├── fast_signals.c     # Driver SPI para AD7606 x2
│   │   └── spi_link_slave.c   # Enlace SPI con el Maestro (modo Esclavo)
│   ├── include/
│   │   ├── pin_config.h       # Configuración de pines del RP2040
│   │   └── FreeRTOSConfig.h   # Configuración del kernel FreeRTOS
│   └── lib/
│       └── FreeRTOS-Kernel/   # Kernel FreeRTOS (submodule de Git)
└── FDR.code-workspace   # Workspace de VS Code multi-carpeta
```

### Tareas y Prioridades (FreeRTOS) *Contempladas hasta el momento*
El sistema prioriza la sincronización inter-placas y la adquisición en tiempo real con la siguiente jerarquía:

| Prioridad | Tarea | Placa | Función |
|:---------:|-------|:-----:|---------|
| 8 | `tsk_coordinator_slave` | Esclava | Espera `SYNC_TRIG` del Maestro, despierta las demás tareas y se autodestruye. |
| 7 | `tsk_acq_fast_slave` | Esclava | Dispara conversión, lee AD7606 vía SPI0, envía a `q_fast_data` (~100 Hz). |
| 6 | `tsk_spi_link_slave` | Esclava | Empaqueta con Header + CRC y transmite al Maestro vía SPI1 (Slave). |
| 6 | `tsk_spi_link_master` | Maestra | Recibe paquetes del Esclavo vía SPI (Master). |
| 5 | `tsk_data_proc` | Maestra | Recibe datos y los encamina al almacenamiento (SD/eMMC). |

## Requisitos y Configuración de Desarrollo

### Placa Maestra (ESP32-S3)
*   **Framework:** Espressif IoT Development Framework (ESP-IDF).
*   **Lenguaje:** C.
*   **IDE Recomendado:** Visual Studio Code con la extensión ESP-IDF.

### Placa Esclava (RP2040-Zero)
*   **Framework:** Raspberry Pi Pico SDK + FreeRTOS (como submódulo de Git).
*   **Lenguaje:** C.
*   **IDE Recomendado:** Visual Studio Code con la extensión Raspberry Pi Pico.

### Clonado del Repositorio
Al incluir FreeRTOS-Kernel como submódulo de Git, se debe clonar el repositorio con:
```bash
git clone --recurse-submodules https://github.com/Moisespg13/Flight-Data-Recorder.git
```
Si ya se clonó sin submódulos:
```bash
git submodule update --init --recursive
```

### Gestión de Versiones
*   **Herramientas:** Git y GitHub.
*   **Workspace:** Se provee un archivo `FDR.code-workspace` para abrir las tres subcarpetas simultáneamente en VS Code.

## Estándares y Normativa
Aunque el sistema fue concebido para vuelos de ensayo y no pretende una certificación civil comercial estricta, su firmware y hardware se desarrollan incorporando principios de normativas aeronáuticas como RTCA DO-178C (Desarrollo de Software), RTCA DO-160G (Entorno) y EUROCAE ED-112A (Crash Protected Recorders).

## Créditos
Desarrollado como proyecto de Trabajo Final de Grado e integrado al proyecto de extensión FDR en el Instituto Universitario Aeronáutico (IUA).
