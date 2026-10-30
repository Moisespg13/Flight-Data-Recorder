# imu_xsens — Unidad de Medición Inercial (IMU)

Componente encargado de la comunicación y decodificación de datos de actitud, aceleración y velocidad angular provenientes de la unidad inercial **Xsens MTi-28A53G35** montada a bordo de la aeronave.

---

## Interfaz de Hardware

* **Periférico Serie:** UART0 del ESP32-S3 a 115200 baudios, 8N1.

| Señal | GPIO | Descripción / Conexión |
|-------|:----:|------------------------|
| **TX** | 43 | Salida serie del ESP32-S3 hacia la entrada RX de la IMU |
| **RX** | 44 | Entrada serie del ESP32-S3 desde la salida TX de la IMU |

---

## Protocolo de Comunicación y Mapeo en Tramas CEV

El componente implementa el protocolo binario propietario MT de Xsens (tramas delimitadas por byte de preámbulo `0xFA`, identificador de mensaje, longitud de carga útil, datos y suma de verificación de 8 bits).

A partir de los paquetes decodificados por la tarea inercial, se extraen y normalizan las siguientes variables dinámicas, las cuales se cargan en el búfer de parámetros para su sub-conmutación en las palabras `DSP` (Data Stream Parameters):

| Identificador CEV | Parámetro Dinámico | Unidad / Rango |
|:-----------------:|--------------------|----------------|
| **`DSP_1`** | Aceleración en eje X | $m/s^2$ |
| **`DSP_2`** | Aceleración en eje Y | $m/s^2$ |
| **`DSP_3`** | Aceleración en eje Z | $m/s^2$ |
| **`DSP_4`** | Velocidad angular en eje X (Roll rate) | $^{\circ}/s$ |
| **`DSP_5`** | Velocidad angular en eje Y (Pitch rate) | $^{\circ}/s$ |
| **`DSP_6`** | Velocidad angular en eje Z (Yaw rate) | $^{\circ}/s$ |
| **`DSP_7`** | Campo magnético en eje X | Unidades arbitrarias (a.u.) |
| **`DSP_8`** | Campo magnético en eje Y | Unidades arbitrarias (a.u.) |
| **`DSP_9`** | Campo magnético en eje Z | Unidades arbitrarias (a.u.) |
| **`DSP_10`** | Ángulo de alabeo (Roll) | Grados ($^{\circ}$) |
| **`DSP_11`** | Ángulo de cabeceo (Pitch) | Grados ($^{\circ}$) |
| **`DSP_12`** | Estado de sincronización PPS | Bandera binaria (`0` o `1`, no proviene de la IMU sino del GNSS) |
| **`DSP_13`** | Ángulo de guiñada (Yaw / Heading) | Grados ($^{\circ}$) |
