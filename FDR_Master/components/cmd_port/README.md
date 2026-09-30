# cmd_port

Puerto de comandos RS-232. UART1 a 115200 baudios, 8N1, sin control de flujo.

| Señal | GPIO | Hacia el MAX3232 |
|-------|:----:|------------------|
| RX | 41 | Sale del receptor (pin 3 del DB9 entra acá, en 3,3 V) |
| TX | 42 | Entra al transmisor (sale por el pin 2 del DB9) |

El MAX3232 traduce 3,3 V a los ±10 V del RS-232. El DB9 de la placa es hembra, cableado para un cable derecho hacia el puerto macho de la PC.

Cada mensaje son 6 bytes, little-endian: `0xAA55`, comando, dato.

| CMD | Función |
|:---:|---------|
| 1 | Lee un canal (0–31 analógico, 32–47 discreta) |
| 2 | Guarda el formato CEV (1, 2 o 3) en NVS |
| 3 | Lee el formato guardado. Si no hay ninguno, responde 1 |
| 4 | Formatea las MicroSD. El dato tiene que ser `0x55AA` |

La respuesta repite el encabezado y el comando. `0x0001` es ACK y `0xFFFF` es error. El comando 2 no cambia la grabación en curso.
