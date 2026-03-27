# FDR_Slave – RP2040-Zero (Waveshare)

Firmware del Esclavo de adquisición basado en la **RP2040-Zero** de Waveshare, funcionando bajo **FreeRTOS SMP** (doble núcleo).

## Responsabilidades
- **Núcleo 0 (Tarea ACQ):** Adquisición de señales analógicas (AD7606) vía SPI0.
- **Núcleo 1 (Tarea SPI_TX):** Enlace inter-placa con el Maestro (ESP32-S3) vía SPI1 (Slave).
- **Sincronización:** Espera de señal `SYNC_TRIG` del Maestro para iniciar la adquisición coordinada.

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
- **Inter-Task Communication:** Se utiliza una `QueueHandle_t` para pasar los payloads de adquisición a la tarea de transmisión sin bloquear el muestreo.
- **Configuración:** Ver `include/FreeRTOSConfig.h`.
