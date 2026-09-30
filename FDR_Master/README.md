# FDR_Master — ESP32-S3

Firmware de la placa maestra de la PCB. ESP-IDF v5.5. Junta lo que manda el RP2040 con el GNSS, la IMU Xsens y las 16 discretas, arma tramas CEV y las graba en dos MicroSD.

Si al encender hay un pendrive, entra en modo descarga (USB Host MSC) y no graba. Si no hay pendrive, entra en modo vuelo.

## Memoria

La PSRAM octal de 8 MB está activada a 40 MHz (`sdkconfig.defaults`). Los GPIO 33–37 quedan tomados por esa memoria. El anillo de grabación (2 MB) vive ahí. Las pilas de las tareas quedan en la SRAM interna.

## Tareas

| Tarea | Prioridad | Núcleo | Qué hace |
|-------|:---------:|:------:|----------|
| `tsk_spi_link` | 12 | 1 | Recibe el enlace SPI2 (DRDY por interrupción) |
| ensamblado CEV | 9 | 1 | Arma frames y los copia al anillo |
| procesado rápido y lento | 5 | 1 | Pasa las muestras del esclavo al formato CEV |
| escritura SD | 6 | 0 | Vacía el anillo a las dos MicroSD |
| time sync | 7 | — | NMEA y PPS |
| discretas, IMU | 5 | — | TCA9555 y Xsens |
| comandos | 4 | — | UART1 RS-232 |
| supervisor | 3 | — | Check-in de las tareas |

## Grabación

Cada bloque que sale del anillo es de hasta 16 KB. Las dos SD reciben los mismos bytes, una detrás de la otra (MISO compartido en GPIO 40). El `fsync` no es simultáneo: SD1 a los 2, 4, 6… minutos y SD2 a los 3, 5, 7…. Si el anillo está más de la mitad lleno, ese `fsync` se corre 60 s. Ante power-loss (GPIO 46) se vacía el anillo y se cierran las dos.

## Compilar

```bash
cd FDR_Master
idf.py build
idf.py -p COMx flash monitor
```

Los logs salen por USB-Serial-JTAG. El UART1 (GPIO 41/42, MAX3232) es solo el puerto de comandos.
