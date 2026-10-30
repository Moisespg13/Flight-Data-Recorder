# sd_storage — Almacenamiento Redundante en Tarjetas MicroSD

Componente encargado del registro simultáneo de datos de vuelo en dos tarjetas MicroSD independientes, utilizando el bus periférico SPI3 del ESP32-S3 a 20 MHz con el sistema de archivos FAT32 (integración `esp_vfs_fat_sdspi`).

---

## Interfaz de Hardware y Buses

Las dos tarjetas MicroSD comparten las líneas principales del bus SPI3 y disponen de líneas de selección de chip (CS) dedicadas:

| Señal | GPIO | Descripción / Conexión |
|-------|:----:|------------------------|
| **MOSI** | 38 | Master Out Slave In compartido hacia ambas ranuras |
| **SCK** | 39 | Reloj SPI compartido a 20 MHz |
| **MISO** | 40 | Master In Slave Out compartido desde ambas ranuras |
| **CS1** | 1 | Chip Select para MicroSD #1 (`/sd1`) |
| **CS2** | 2 | Chip Select para MicroSD #2 (`/sd2`) |

---

## Arquitectura de Grabación y Resiliencia

### 1. Escritura por Bloques de 32 KB (`WRITE_CHUNK_SIZE`)
El volcado de datos desde el búfer circular de PSRAM se realiza en bloques de **32 KB** (`32768 bytes`), dimensión que coincide con el tamaño estándar de clúster de asignación de una partición FAT32 formateada en tarjetas SD de alta capacidad (SDHC/SDXC). Esta alineación evita operaciones internas de lectura-modificación-escritura en el controlador de la memoria flash, maximizando la velocidad sostenida de transferencia.

### 2. Aislamiento Automático de Fallas de Tarjeta
Si durante la grabación una de las tarjetas sufre un error de comunicación, expulsión imprevista o sobrepasa el tiempo límite de espera de escritura, el driver marca internamente la bandera de falla (`sd1_failed = true` o `sd2_failed = true`). A partir de ese instante, la tarjeta dañada queda aislada y el sistema continúa grabando con normalidad en la tarjeta restante, sin trabar el bus SPI ni degradar el flujo continuo de datos.

### 3. Sincronización Desfasada (`fsync`)
Para prevenir pérdidas de datos catastróficas ante un apagado brusco sin sobrecargar el bus con escrituras de metadatos continuas, se implementa una rutina de sincronización temporalmente alternada:
* **Tarjeta SD1:** Ejecuta `fsync` en los minutos pares (minuto 2, 4, 6, 8...).
* **Tarjeta SD2:** Ejecuta `fsync` en los minutos impares (minuto 3, 5, 7, 9...).
* **Inhibición Dinámica por Carga:** Si la ocupación del búfer circular en PSRAM supera el 50% de su capacidad al momento programado del `fsync`, la operación se pospone automáticamente por 60 segundos para priorizar la absorción de telemetría entrante.

### 4. Detección de Falla de Alimentación (Power-Loss)
Al dispararse la interrupción por caída de tensión en GPIO 46, la función de emergencia `sd_storage_flush()` es invocada para vaciar inmediatamente los bloques pendientes en memoria, forzar el volcado final en ambas tarjetas y cerrar los descriptores de archivo antes de que caiga el riel de 3,3 V.

---

## Gestión de Espacio y Mantenimiento

* **Verificación de Espacio Libre:** Previo a la apertura de un nuevo vuelo, el sistema exige un mínimo de **1 GB de espacio libre** en cada partición montada. En caso de no disponer de dicho volumen, una rutina de rotación circular elimina automáticamente el archivo de vuelo más antiguo (`FDR_XXXX.bin`) hasta liberar el espacio requerido.
* **Formateo de Tarjetas:** La función `sd_storage_format()` ejecuta el borrado físico y la recreación del sistema de archivos FAT32 en ambas tarjetas al recibir el comando `0x04` con la clave de confirmación `0x55AA` desde el puerto RS-232.
