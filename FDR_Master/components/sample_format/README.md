# sample_format — Formateo y Ensamblado de Tramas CEV

Componente responsable de ensamblar las palabras de 16 bits de la trama del registrador de vuelo conforme al estándar de sub-conmutación de telemetría y ensayos en vuelo del Centro de Ensayos en Vuelo (CEV).

---

## Formatos de Registro Soportados

El sistema soporta tres modalidades de empaquetado configurables según el tipo de ensayo y el ancho de banda requerido:

| Formato | Frames por Ciclo | Palabras (Words) por Frame | Tasa de Ciclos | Tasa de Frames | Flujo de Datos Bruto |
|:-------:|:----------------:|:--------------------------:|:--------------:|:--------------:|:--------------------:|
| **Formato 1** | 40 | 17 | 75 ciclos/s | 3000 frames/s | ~102 KB/s (522 KB/s máx.) |
| **Formato 2** | 40 | 11 | 75 ciclos/s | 3000 frames/s | ~66 KB/s |
| **Formato 3** | 10 | 6  | 10 ciclos/s | 100 frames/s  | ~1,2 KB/s |

---

## Estructura de la Trama y Sub-conmutación (Formato 1)

En el Formato 1, cada frame contiene 17 palabras de 16 bits:
* **Palabras 1 a 16 (Canales Analógicos Rápidos):** Contienen de manera fija los canales analógicos `ANA_1` a `ANA_16` adquiridos por los convertidores AD7606 a la frecuencia plena de 3000 muestras por segundo por canal.
* **Palabra 17 (Canal Sub-conmutado):** Rota cíclicamente a lo largo de los 40 frames de cada ciclo mayor para transmitir parámetros de menor dinámica temporal:
  * **Frame 1:** Palabra de entradas discretas `DIG` (16 bits empaquetados del TCA9555).
  * **Frames 2 a 17:** Canales analógicos lentos `ANA_17` a `ANA_32` (adquiridos a ~20 Hz por el AD7490).
  * **Frames 18 a 30:** Parámetros de la IMU Xsens (`DSP_1` a `DSP_11` y `DSP_13`), que incluyen aceleraciones triaxiales, velocidades angulares, magnetómetro y ángulos de actitud.
  * **Frame 31:** Flag de sincronismo satelital `DSP_12` (indicador binario de pulso PPS enganchado).
  * **Frames 32 a 37:** Estampa de tiempo UTC decodificada del receptor GNSS (`HOR`, `MIN`, `SEG`, `DIA`, `MES`, `ANO`).
  * **Frames 38 a 40:** Reservados / diagnóstico de integridad.

Los Formatos 2 y 3 reducen la cantidad de canales rápidos a fin de priorizar la autonomía de almacenamiento en vuelos de larga duración o registrar parámetros primarios de vuelo, integrando adicionalmente latitud, longitud y altitud geodésica (`LAT`, `LON`, `ALT`).

---

## Configuración y Persistencia

* La función `sample_format_init()` lee el valor configurado al arrancar desde la partición NVS (`namespace: "storage"`, clave: `"sample_fmt"`). Si la clave no existe, se adopta por defecto el Formato 1.
* El formato puede modificarse en tierra enviando el comando `0x02` a través de la interfaz RS-232 (`cmd_port`). Esta acción actualiza el valor en NVS para los próximos arranques, sin alterar la estructura del archivo binario que ya se encuentre en grabación activa.
