# master_logic

Arranque del modo vuelo y reparto de tareas del maestro.

Lee el formato CEV desde NVS (`sample_fmt`, por defecto 1), abre `FDR_XXXX.bin` en las dos SD y reserva el anillo de 2 MB en PSRAM (`heap_caps_aligned_alloc`, alineado a 32 bytes). El ensamblado (núcleo 1, prioridad 9) copia frames a ese anillo y no descarta un frame a medias: un hueco desalinea el archivo. `sd_io` (núcleo 0, prioridad 6) es la única tarea que escribe las tarjetas y la que aplica el `fsync` desfasado.

La ISR de power-loss en GPIO 46 avisa al ensamblador. `sd_io` vacía el anillo, hace `fsync` de las dos y cierra.

También crea las tareas de GNSS, IMU, discretas, comandos y supervisor.
