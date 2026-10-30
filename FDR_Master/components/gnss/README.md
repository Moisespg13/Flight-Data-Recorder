# gnss — Receptor Satelital de Posición y Tiempo

Componente encargado de la adquisición de datos de navegación satelital (hora UTC, fecha, coordenadas de latitud, longitud y altitud geodésica) y de la sincronización temporal mediante el pulso por segundo (PPS) de alta precisión.

---

## Interfaz de Hardware y Asignación de Pines

* **Periférico Serie:** UART2 del ESP32-S3 a 9600 baudios, 8N1.
* **Captura de PPS:** Entrada GPIO con interrupción por flanco de subida.

| Señal | GPIO | Descripción / Conexión |
|-------|:----:|------------------------|
| **PPS** | 15 | Pulso por segundo del módulo GNSS (captura con resolución sub-microsegundo) |
| **TX** | 16 | Salida serie del ESP32-S3 hacia la entrada RX del receptor GNSS |
| **RX** | 17 | Entrada serie del ESP32-S3 desde la salida TX del receptor GNSS |

> **Nota de Diseño:** En revisiones tempranas del esquema se asignaron los GPIO 35, 36 y 37 a esta interfaz. Dichos pines quedaron reservados exclusivamente por hardware para el bus de datos octal de la memoria PSRAM (GPIO 33 a 37), motivo por el cual la interfaz GNSS se reubicó de manera definitiva a los GPIO 15, 16 y 17. La misma señal física de PPS de 1 Hz se distribuye en paralelo hacia la placa esclava RP2040 en su pin GP26.

---

## Operación del Driver

1. **Recepción y Decodificación NMEA:**
   * La tarea de sincronización temporal recibe el flujo continuo de sentencias ASCII del receptor.
   * Procesa principalmente las sentencias `$GPRMC` y `$GPGGA`, extrayendo la hora exacta UTC (horas, minutos, segundos), fecha (día, mes, año), estado del enganche satelital (Fix valid / invalid), latitud, longitud y altitud.
2. **Sincronización por Interrupción PPS:**
   * Cada flanco ascendente en GPIO 15 dispara una rutina de interrupción rápida (ISR) que actualiza el flag de sincronización (`DSP_12`) y permite disciplinar la base de tiempo de grabación de la placa maestra con precisión de microsegundos respecto a la escala UTC.
3. **Publicación en el Formato CEV:**
   * Los valores decodificados se escriben en el búfer de `sample_format` para su inserción en los ciclos de sub-conmutación correspondientes (campos temporales `HOR`, `MIN`, `SEG`, `DIA`, `MES`, `ANO` y canales de posición `LAT`, `LON`, `ALT`).
