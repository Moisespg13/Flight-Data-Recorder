# gnss

Receptor GNSS por UART2 a 9600 baudios. Parsea NMEA (hora UTC y posición) y toma el PPS por interrupción.

| Señal | GPIO |
|-------|:----:|
| RX (entra la TX del módulo) | 17 |
| TX (sale hacia el RX del módulo) | 16 |
| PPS | 15 |

Esos pines no son los 35/36/37 del diseño viejo: la PSRAM octal usa GPIO 33–37. El mismo PPS se cablea al RP2040 en GP26.

`gnss_process` corre en la tarea de sincronización. Publica hora, posición y el estado del PPS en el buffer de `sample_format`.
