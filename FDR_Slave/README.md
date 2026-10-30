# FDR_Slave — RP2040-Zero (Waveshare)

Firmware del subsistema esclavo de adquisición de datos basado en el microcontrolador de doble núcleo **RP2040** (módulo Waveshare RP2040-Zero, dual-core ARM Cortex-M0+ a 133 MHz), operando bajo el sistema operativo en tiempo real **FreeRTOS SMP** (Symmetric Multiprocessing).

El esclavo tiene como función exclusiva la adquisición analógica multicanal sincronizada, el acondicionamiento primario de señales, la sincronización temporal disciplinada por satélite (PPS) y la transmisión de alta velocidad hacia la placa maestra (ESP32-S3) a través de un enlace serie controlado por hardware programable (PIO).

---

## Responsabilidades del Módulo

1. **Adquisición Analógica Rápida (3 kHz):**
   * Controla dos convertidores analógico-digitales **AD7606** (16 bits, bipolar ±10 V, 8 canales por integrado, 16 canales totales).
   * Disparo síncrono mediante pulso negativo en la línea compartida `CONVST` (GP10) comandado por un temporizador de hardware a **333 µs** (~3003 Hz).
   * Lectura secuencial de ambos chips sobre el bus SPI0 a 10 MHz en **Modo 2 (CPOL=1, CPHA=0)**.
2. **Adquisición Analógica Lenta (20 Hz):**
   * Controla un convertidor analógico-digital **AD7490** (12 bits, unipolar 0 a $V_{REF}$, 16 canales independientes) en SPI0 con selección por `CS` (GP9).
   * Tasa de actualización de 20 Hz (cada 50 ms).
   * Gestión coordinada de bus mediante un semáforo mutex: reconfigura dinámicamente SPI0 a **Modo 3 (CPOL=1, CPHA=1)** para comunicarse con el AD7490 y restablece el Modo 2 al finalizar.
   * Alineación de pipeline con trama dummy de cebado y verificación en tiempo real de los 4 MSB (`ADD3..ADD0`) para detectar desincronizaciones de canal.
3. **Enlace Inter-Placa por PIO + DMA (Hacia ESP32-S3):**
   * Implementa un esclavo SPI de solo transmisión mediante una máquina de estados del bloque **PIO** (`spi_slave_tx.pio`) y un canal **DMA de 32 bits**, prescindiendo del periférico síncrono PL022 de silicio del RP2040 para evitar desbordes y bloqueos de búfer.
   * Handshake por hardware mediante línea **DRDY** (GP3): el esclavo eleva DRDY cuando el paquete está preparado en memoria; el maestro baja CS y emite reloj de lectura a 10 MHz; el esclavo baja DRDY al detectar la subida de CS.
   * **Agrupamiento de muestras rápidas:** Empaqueta **4 muestras consecutivas** en cada trama SPI (178 bytes totales), reduciendo la frecuencia de interrupciones del maestro a 750 Hz y optimizando el ancho de banda.
4. **Base de Tiempo Disciplinada por GNSS (PPS):**
   * Entrada de pulso por segundo (PPS) en GP26 conectada a una interrupción GPIO de respuesta inmediata (< 1 µs de jitter).
   * Corrige progresivamente la deriva del oscilador de cuarzo del RP2040 (`s_pps_offset_us`), asignando a cada muestra analógica un timestamp en nanosegundos alineado con el tiempo satelital UTC.
5. **Arranque Coordinado:**
   * La tarea `tsk_coordinator_slave` aguarda el primer flanco de bajada de la línea CS emitida por la placa maestra para desbloquear y sincronizar a las tareas de adquisición y transmisión, autodestruyéndose a continuación para liberar memoria stack y ciclos de CPU.
6. **Watchdog por Hardware:**
   * Temporizador perro guardián por hardware con tiempo límite de 2000 ms. Es alimentado periódicamente por una tarea de mínima prioridad (Prioridad 1); si alguna tarea de adquisición o transmisión sufre un bloqueo o lazo infinito, el watchdog fuerza el reinicio automático del microcontrolador.

---

## Mapa de Pines GPIO (RP2040-Zero)

| Pin GPIO | Nombre / Función | Periférico | Descripción / Conexión |
|:--------:|------------------|:----------:|------------------------|
| **GP0** | `PIN_LINK_CS` | PIO (Entrada) | Chip Select desde ESP32-S3 (GPIO 10) |
| **GP1** | `PIN_LINK_TX` | PIO (Salida) | MISO de datos hacia ESP32-S3 (GPIO 13) |
| **GP2** | `PIN_LINK_SCK` | PIO (Entrada) | Reloj síncrono desde ESP32-S3 (GPIO 12) |
| **GP3** | `PIN_DRDY` | GPIO (Salida) | Data Ready de aviso de paquete listo (GPIO 11 Maestra) |
| **GP4** | `PIN_ACQ_MISO` | SPI0 RX | MISO compartido de los ADCs (AD7606 x2 y AD7490) |
| **GP5** | `PIN_CS_AD7606_1`| GPIO (Salida) | Chip Select AD7606 #1 (canales analógicos 1 a 8) |
| **GP6** | `PIN_ACQ_SCK` | SPI0 SCK | Reloj de adquisición hacia los ADCs (10 MHz) |
| **GP7** | `PIN_ACQ_MOSI` | SPI0 TX | MOSI de configuración hacia el AD7490 |
| **GP9** | `PIN_CS_AD7490` | GPIO (Salida) | Chip Select AD7490 (canales lentos 17 a 32) |
| **GP10** | `PIN_ADC_CONVST` | GPIO (Salida) | Señal de inicio de conversión simultánea para ambos AD7606 |
| **GP12** | `PIN_ADC_BUSY` | GPIO (Entrada) | Línea Busy de fin de conversión de AD7606 #1 |
| **GP13** | `PIN_STBY` | GPIO (Entrada) | Pulsador de Stand-By / servicio |
| **GP15** | `PIN_ADC_RST` | GPIO (Salida) | Pulso de reinicio por hardware compartido de los AD7606 |
| **GP16** | `PIN_LED_WS2812`| NeoPixel | LED RGB integrado en placa RP2040-Zero |
| **GP26** | `PIN_PPS` | GPIO (Entrada) | Entrada de pulso PPS de sincronismo temporal (desde GNSS) |
| **GP27** | `PIN_CS_AD7606_2`| GPIO (Salida) | Chip Select AD7606 #2 (canales analógicos 9 a 16) |
| **GP28** | `PIN_ADC2_BUSY` | GPIO (Entrada) | Línea Busy de fin de conversión de AD7606 #2 |
| **GP11, GP29** | Libres | GPIO | Reservados para expansión futura |

---

## Estructura del Código Fuente (`src/` e `include/`)

El código del esclavo se divide en módulos funcionales bien delimitados:

| Archivo | Rol y Descripción Detallada |
|---------|-----------------------------|
| [src/main.c](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Slave/src/main.c) | Punto de entrada, inicialización de FreeRTOS SMP, creación de colas, semáforos, tareas concurrentes, interrupciones compartidas y lazo del reloj disciplinado por PPS. |
| [src/fast_signals.c](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Slave/src/fast_signals.c) | Driver de control de los dos convertidores AD7606. Generación del pulso CONVST, espera no bloqueante de BUSY y lectura de 16 palabras de 16 bits en SPI0 Modo 2. |
| [src/slow_signals.c](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Slave/src/slow_signals.c) | Driver del convertidor multiplexado AD7490 (16 canales). Reconfiguración de SPI0 a Modo 3, transmisión de palabra de control, lectura desfasada de conversiones y chequeo de canal. |
| [src/spi_link_slave.c](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Slave/src/spi_link_slave.c) | Orquestador del enlace con la maestra. Agrupa las muestras rápidas de a 4, conforma encabezados con CRC32, alimenta el DMA hacia el FIFO del PIO y gestiona el handshake DRDY/CS. |
| [src/spi_slave_tx.pio](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Slave/src/spi_slave_tx.pio) | Programa en ensamblador PIO de la RP2040. Emite bits síncronos por MISO ante el reloj del maestro mientras la línea CS permanezca en bajo. |
| [include/pin_config.h](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Slave/include/pin_config.h) | Definiciones definitivas de asignación de terminales GPIO de la placa esclava. |
| [include/FreeRTOSConfig.h](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Slave/include/FreeRTOSConfig.h) | Parámetros del planificador FreeRTOS SMP (núcleos simétricos, memoria dinámica heap_4, hooks de desborde). |

> Para un análisis exhaustivo a nivel de funciones y flujo de datos de cada archivo `.c`, consultar el documento específico: [src/README.md](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Slave/src/README.md).

---

## Requisitos de Software

* [Raspberry Pi Pico SDK v2.2.0](https://github.com/raspberrypi/pico-sdk)
* Cadena de herramientas GNU ARM Embedded Toolchain (`arm-none-eabi-gcc`)
* CMake (versión 3.13 o superior) y generador Ninja

---

## Preparación del Entorno (Kernel FreeRTOS)

El firmware requiere el repositorio del núcleo de FreeRTOS dentro del directorio `lib/`:

```powershell
cd FDR_Slave
if (!(Test-Path lib)) { New-Item -ItemType Directory -Path lib }
cd lib
if (!(Test-Path FreeRTOS-Kernel)) {
    git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
}
```

---

## Compilación y Flasheo

### 1. Compilación por Terminal
```powershell
cd FDR_Slave
mkdir build
cd build
cmake -G "Ninja" ..
ninja
```

### 2. Flasheo en la Placa RP2040-Zero
1. Conectar la placa RP2040-Zero a la computadora mediante cable USB-C.
2. Mantener presionado el pulsador **BOOT**, pulsar una vez el pulsador **RESET** y soltar **BOOT**. La placa se montará en el sistema operativo como una unidad de almacenamiento masivo con la etiqueta `RPI-RP2`.
3. Arrastrar o copiar el archivo binario generado `FDR_Slave.uf2` hacia la unidad `RPI-RP2`. El microcontrolador se reiniciará automáticamente ejecutando el nuevo firmware.

---

## Monitoreo de Diagnóstico Serie

El firmware expone una consola de registro a través del controlador serie USB CDC integrado:
* **Dispositivo:** Puerto COM asignado en Administrador de Dispositivos (Windows).
* **Velocidad:** 115200 baudios, 8N1.
* Proporciona mensajes de arranque, estado del coordinador, sincronismo de PPS con deriva del oscilador en microsegundos y estadísticas periódicas de ráfagas transmitidas.
