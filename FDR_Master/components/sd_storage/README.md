# sd_storage

Dos MicroSD en SPI3 a 20 MHz. MISO compartido.

| Señal | GPIO |
|-------|:----:|
| MOSI | 38 |
| CLK | 39 |
| MISO | 40 |
| CS1 | 1 |
| CS2 | 2 |

Monta `/sd1` y `/sd2`. La grabación sigue si al menos una responde. Un mutex serializa el acceso.

`sd_storage_write` acumula en un buffer de 16 KB y, al llenarse, hace `fwrite` en las dos. No hace `fsync` en ese camino. `sd_storage_flush_1` y `sd_storage_flush_2` vuelcan el buffer pendiente y sincronizan solo esa tarjeta. `sd_storage_flush` sincroniza las dos (cierre de emergencia).

Antes de abrir un vuelo exige 1 GB libre. Si no alcanza, borra el `FDR_XXXX.bin` más viejo. `sd_storage_format` borra la tarjeta (comando 4 del puerto serie).
