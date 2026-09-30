# FDR Converter

Herramienta de post-proceso para archivos de vuelo del FDR (CEV-FAA / IUA).

## ¿Qué hace?

- Lee archivos `FDR_XXXX.bin` generados por el firmware del ESP32-S3 (Placa A).
- Decodifica el formato CEV de sub-conmutación (Formato 1, 2 o 3 — autodetectado desde el header).
- Exporta los datos a `.csv` con una columna por parámetro y una fila por ciclo.
- Muestra gráficos interactivos con zoom/pan para análisis en tierra.
- Lee el `.bin` por bloques (una barra abajo muestra los MB leídos). Por encima de 40 MB no genera el CSV solo: hay que usar **Guardar CSV**.
- En Configuración, **Leer del FDR** y **Guardar en el FDR** mandan los comandos 3 y 2 por el UART de comandos. El formato queda en la NVS y se usa al reiniciar el equipo, no en la grabación ya abierta.

## Uso en el hangar (sin internet)

1. Copiar `FDR_Converter.exe` al pendrive junto con los archivos `.bin`.
2. Doble clic en `FDR_Converter.exe` (no requiere Python instalado).
3. Seleccionar el archivo `.bin` con el botón `…`.
4. Presionar **Convertir y Graficar**.
5. Guardar el `.csv` resultante con **Guardar CSV**.

## Formato del CSV de salida

| Columnas | Descripción |
|---|---|
| `ciclo` | Número de ciclo CEV (1 = primer ciclo) |
| `tiempo_s` | Tiempo en segundos desde el inicio del archivo |
| `ANA_1` … `ANA_32` | Canales analógicos en LSB del ADC (ANA 1–16 = rápidos, 17–32 = lentos) |
| `DIG_1` … `DIG_16` | Señales discretas (0 o 1) |
| `DSP_1` … `DSP_3` | Aceleración X/Y/Z (m/s²) |
| `DSP_4` … `DSP_6` | Giróscopo X/Y/Z (°/s) |
| `DSP_7` … `DSP_9` | Magnetómetro X/Y/Z (a.u.) |
| `DSP_10`, `DSP_11` | Roll, Pitch (°) |
| `DSP_12` | Señal PPS Sync (0/1) |
| `DSP_13` | Yaw / guiñada (°) |
| `DIA`, `MES`, `ANO`, `HOR`, `MIN`, `SEG` | Timestamp GNSS |
| `LAT`, `LON`, `ALT` | Posición GNSS reconstruida (grados y pies) |

## Compilar el .exe (solo si necesitás regenerarlo)

```
pip install -r requirements.txt
build_exe.bat
```

El `.exe` estará en `dist\FDR_Converter.exe`.

## Tasas de muestreo por formato

| Formato | Ciclos/seg | Frames totales | Velocidad de datos |
|---|---|---|---|
| F1 (40×17 words) | 75 | 3000 fr/s | ~102 KB/s |
| F2 (40×11 words) | 75 | 3000 fr/s | ~66 KB/s |
| F3 (10×6 words)  | 10 | 100 fr/s  | ~1.2 KB/s |
