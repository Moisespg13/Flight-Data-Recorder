# discrete_signals

16 entradas digitales con un TCA9555 por I2C.

| Señal | GPIO |
|-------|:----:|
| SCL | 4 |
| SDA | 5 |

Dirección `0x20`, 400 kHz. La lectura la hace `tsk_discrete_master` a 20 Hz. Un bit entra a la trama CEV recién después de 3 muestras consecutivas iguales (debounce por software). El bitmap de 16 bits es la palabra `DIG` del formato.
