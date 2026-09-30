# FDR_Slave – RP2040-Zero (Waveshare)

Firmware del Esclavo de adquisición basado en la **RP2040-Zero** de Waveshare, funcionando bajo **FreeRTOS SMP** (doble núcleo).

## Responsabilidades
- **Adquisición:** 2× AD7606 (16 canales a 3 kHz) y 1× AD7490 (16 canales a ~20 Hz) en SPI0, con mutex. El AD7490 usa modo SPI 3 y el AD7606 modo 2; el driver lento cambia el modo del bus mientras lo tiene.
- **Enlace:** esclavo SPI por PIO + DMA hacia el ESP32-S3 (CS GP0, MISO GP1, SCK GP2, DRDY GP3). No usa el SPI de hardware del RP2040. Cada paquete rápido lleva 4 muestras (178 bytes); cada muestra conserva su timestamp.
- **Arranque:** el coordinador espera el primer flanco de bajada de CS. No hay línea `SYNC_TRIG`.
- **Tiempo:** el PPS entra por GP26 y corrige el offset del contador local.
- **Watchdog:** hardware, timeout 2000 ms, pateado por una tarea de prioridad 1.

## Requisitos de Software
- [Pico SDK v2.2.0](https://github.com/raspberrypi/pico-sdk)
- Toolchain ARM GCC (`arm-none-eabi-gcc`)
- CMake >= 3.13

## Preparación del Entorno (FreeRTOS)
Este proyecto requiere el kernel de FreeRTOS dentro de la carpeta `lib/`. Si no existe, ejecutar:

```powershell
cd FDR_Slave
mkdir lib
cd lib
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
```

## Compilación y Flasheo

1. **Compilar:** Usar la extensión de VS Code "Raspberry Pi Pico" (botón **Compile**) o vía terminal:
   ```bash
   mkdir build && cd build
   cmake ..
   ninja
   ```
2. **Flashear:**
   - Conectar la placa vía USB, mantener presionado **BOOT** y a su vez presionar **RESET**.
   - Arrastrar el archivo `build/FDR_Slave.uf2` al volumen `RPI-RP2`.

## Monitoreo (Debug)
El puerto serie se expone vía USB (CDC). Usar **PuTTY** o la extensión **Serial Monitor** de VS Code:
- **Puerto:** COMx (puedo verlo desde el Administrador de Dispositivos de Windows).
- **Velocidad:** 115200 baudios.

## Arquitectura de Software
- **FreeRTOS SMP:** Aprovecha el procesador dual-core de la RP2040.
- **Inter-Task Communication:** Dos colas (`q_fast_data`, `q_slow_data`) pasan los payloads a la tarea de transmisión. El enlace prioriza la cola rápida.
- **Configuración:** Ver `include/FreeRTOSConfig.h`.
