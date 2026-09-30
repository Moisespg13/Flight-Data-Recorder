# sample_format

Arma las tramas CEV a partir del buffer de parámetros (analógicos, discretas, IMU, GNSS).

| Formato | Frames por ciclo | Words por frame | Tasa |
|:-------:|:----------------:|:---------------:|------|
| 1 | 40 | 17 | 75 ciclos/s (3000 frames/s) |
| 2 | 40 | 11 | 75 ciclos/s (3000 frames/s) |
| 3 | 10 | 6 | 10 ciclos/s (100 frames/s) |

En el formato 1, las words 1–16 son ANA 1–16 en todos los frames. La word 17 rota: discretas, ANA 17–32, DSP 1–13, hora y fecha. DSP 12 es el sincronismo PPS. DSP 13 es la guiñada. Los formatos 2 y 3 también guardan latitud, longitud y altitud.

`sample_format_init` se llama una vez al arrancar, con el valor leído de NVS. Cambiar el formato por el puerto de comandos no altera el archivo que ya está abierto: hace falta reiniciar.
