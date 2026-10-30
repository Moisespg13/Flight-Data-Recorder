# cmd_port — Puerto de Comandos RS-232

Componente encargado del canal de mantenimiento y diagnóstico en tierra a través de una interfaz serie RS-232 estándar sobre el periférico UART1 del ESP32-S3.

---

## Parámetros de la Interfaz

* **Baudrate:** 115200 baudios.
* **Formato:** 8 bits de datos, sin paridad, 1 bit de parada (8N1).
* **Control de flujo:** Ninguno (deshabilitado por hardware y software).
* **Nivel físico:** El transceptor MAX3232 convierte los niveles lógicos TTL de 3,3 V del ESP32-S3 a los niveles de tensión bipolares de ±10 V del estándar RS-232.
* **Conector:** DB-9 hembra montado en chasis, conectado en configuración directa hacia el puerto macho de la computadora de servicio.

| Señal | GPIO | Dirección / Conexión al MAX3232 |
|-------|:----:|----------------------------------|
| **RX** | 41 | Entrada al ESP32-S3 (sale del receptor R1OUT del MAX3232; pin 3 del DB-9) |
| **TX** | 42 | Salida del ESP32-S3 (entra al transmisor T1IN del MAX3232; pin 2 del DB-9) |

---

## Protocolo de Mensajería

Cada trama de comando intercambiada consta de una longitud fija de 6 bytes en formato little-endian:

```
[0x55] [0xAA] [Comando (1B)] [Reservado/Subcomando (1B)] [Dato (2B)]
```

* **Encabezado fijo:** `0xAA55` (bytes `0x55` y `0xAA` en memoria).

### Tabla de Comandos Soportados

| Comando (CMD) | Función | Parámetro de Entrada (Dato) | Respuesta |
|:-------------:|---------|-----------------------------|-----------|
| **`0x01`** | Lectura directa de canal en tiempo real | Número de canal (0 a 31 analógicos, 32 a 47 discretos) | Devuelve el valor actual de 16 bits del canal solicitado |
| **`0x02`** | Configurar formato CEV activo | Formato deseado (`1`, `2` o `3`) | Guarda el parámetro en NVS y confirma con ACK (`0x0001`) |
| **`0x03`** | Consultar formato CEV guardado | `0x0000` | Devuelve el formato actual configurado en NVS (por defecto `1`) |
| **`0x04`** | Formateo completo de tarjetas MicroSD | Palabra de seguridad `0x55AA` | Formatea particiones FAT32 en SD1 y SD2, devolviendo ACK o ERROR |

### Respuestas del Sistema

La placa maestra responde con un paquete de 6 bytes manteniendo el encabezado y el comando original:
* **Éxito (ACK):** Campo de datos cargado con `0x0001`.
* **Error (NACK):** Campo de datos cargado con `0xFFFF`.

> **Nota:** La modificación del formato CEV mediante el comando `0x02` actualiza la memoria NVS, pero no altera el formato de la grabación que se encuentre activa en ese momento; el nuevo formato entra en vigencia al reiniciar el equipo.
