# master_logic — Lógica de Control y Orquestación del FDR

Componente central del firmware de la placa maestra. Administra el ciclo de vida del sistema, la máquina de estados operativa, la inicialización de tareas concurrentes de FreeRTOS, el búfer circular en memoria PSRAM y la gestión de apagado ante corte imprevisto de energía.

---

## Máquina de Estados Operativa

1. **BOOT / INIT:**
   * Inicialización de periféricos base, bus I2C (expansor TCA9555 y RTC DS3232), UARTs, SPI2 (enlace esclavo) y SPI3 (tarjetas MicroSD).
   * Lectura del formato CEV activo guardado en memoria NVS (`sample_fmt`, por defecto Formato 1).
   * Verificación de presencia de pendrive en el bus USB nativo.
2. **MODO DESCARGA (USB Host MSC):**
   * Se ejecuta si se detectó un pendrive en el inicio. Monta las MicroSD en solo lectura, copia todos los registros de vuelo `.bin` al almacenamiento externo y desmonta los volúmenes de forma segura.
3. **MODO VUELO (FLIGHT RECORDING):**
   * Reserva del **búfer circular de 6 MB en memoria PSRAM** (`heap_caps_aligned_alloc` con alineación a 32 bytes).
   * Creación del archivo de registro `FDR_XXXX.bin` simultáneamente en `/sd1` y `/sd2`.
   * Puesta en marcha de las tareas de adquisición, recepción SPI, ensamblado de tramas y escritura en tarjetas de memoria.
4. **EMERGENCIA / POWER-LOSS:**
   * Detección por hardware de caída de tensión en GPIO 46.
   * Señalización inmediata a las tareas de ensamblado y almacenamiento para vaciar el remanente del búfer circular, sincronizar metadatos FAT32 y cerrar ordenadamente ambos archivos antes del agotamiento de la reserva capacitiva.

---

## Distribución de Núcleos y Tareas de FreeRTOS

Para garantizar determinismo temporal y evitar que las operaciones de almacenamiento interfieran con la recepción de alta velocidad, las tareas se particionan rígidamente entre los dos núcleos de ejecución del ESP32-S3:

### Núcleo 1 (Procesamiento y Recepción en Tiempo Real)
* **`tsk_spi_link` (Prioridad 12):** Tarea crítica despertada por interrupción de hardware ante flancos ascendentes de DRDY (GPIO 11). Clockea ráfagas de 178 bytes (4 muestras analógicas rápidas) a 10 MHz sobre SPI2.
* **`tsk_assemble_cev` (Prioridad 9):** Extrae periódicamente los valores normalizados del búfer de parámetros (analógicos rápidos, lentos, discretas, IMU y GNSS), genera los encabezados y empaqueta las palabras CEV de 16 bits según el formato seleccionado. Deposita los frames completos en el búfer circular de 6 MB en PSRAM mediante escrituras atómicas (nunca inserta frames truncados).
* **`tsk_proc_fast` y `tsk_proc_slow` (Prioridad 5):** Desempaquetan las cargas útiles recibidas por SPI2 y actualizan los canales analógicos del búfer de parámetros.

### Núcleo 0 (Almacenamiento I/O y Servicios del Sistema)
* **`tsk_sd_io` (Prioridad 6):** Tarea exclusiva de volcado a disco. Extrae datos del búfer circular en bloques contiguos de **32 KB** (`WRITE_CHUNK_SIZE`), alineados a los clústeres de FAT32, y ejecuta escrituras `fwrite` en ambas tarjetas MicroSD. Coordina el `fsync` alternado (SD1 en minutos pares, SD2 en impares) y la exclusión de tarjetas que presenten fallas de hardware.
* **`tsk_time_sync` (Prioridad 7):** Decodifica NMEA de UART2, precarga la hora base desde el RTC DS3232 en arranques en frío, y sincroniza la estampa de tiempo UTC mediante interrupciones de PPS en GPIO 15.
* **`tsk_imu_master` (Prioridad 5):** Adquisición de telemetría inercial de la IMU Xsens por UART0.
* **`tsk_discrete_master` (Prioridad 5):** Lectura periódica (20 Hz) y filtrado digital de las 16 entradas discretas del TCA9555 por I2C0.
* **`tsk_cmd_port` (Prioridad 4):** Atención a la consola de mantenimiento y comandos por RS-232 (UART1).
* **`tsk_supervisor` (Prioridad 3):** Monitoreo periódico del latido de todas las tareas del sistema.
* **`tsk_led` (Prioridad 1):** Gestión del parpadeo visual del LED de estado en GPIO 0.

---

## Capacidad del Búfer Circular en PSRAM

El dimensionamiento de **6 MB** en PSRAM resuelve las pausas de latencia típicas de las memorias flash comerciales (desgaste, recolección de basura interna y actualización de tablas FAT32). Con una tasa de flujo de datos máxima de ~522 KB/s, el sistema cuenta con un margen de amortiguación de **~11,5 segundos continuos**, probado y validado en ensayos de esfuerzo ininterrumpido superiores a 3 horas de grabación sin pérdida de un solo frame.
