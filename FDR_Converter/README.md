# FDR Converter — Herramienta de Post-Procesamiento y Visualización en Tierra

Aplicación de escritorio para PC desarrollada en **Python 3** con interfaz gráfica en **Tkinter / Matplotlib / SciPy**, diseñada para decodificar, graficar, analizar en frecuencia y exportar los registros binarios de vuelo generados por el registrador FDR (CEV-FAA / IUA).

---

## Funcionalidades Principales

* **Decodificación de Formato CEV:** Lectura de archivos binarios `FDR_XXXX.bin` o `.fdr`. Autodetecta la estructura de sub-conmutación (Formatos 1, 2 o 3) a partir del encabezado de archivo y separa los canales analógicos rápidos, lentos, discretas, IMU y GNSS.
* **Procesamiento de Archivos de Gran Volumen:** Motor de lectura por bloques contiguos con barra de progreso interactiva y protección contra desbordes de memoria RAM en ensayos prolongados (registros superiores a 500 MB).
* **Visualización Interactiva con Submuestreo Dinámico (LOD):**
  * Gráficos multicanal sincronizados en tiempo real con herramientas de paneo y zoom milimétrico.
  * Implementación de algoritmo de diezmado dinámico por nivel de detalle (**MinMax LOD**), que preserva picos y valles extremos al graficar millones de puntos sin ralentizar la interfaz gráfica de usuario.
* **Análisis Espectral por Transformada Rápida de Fourier (FFT):**
  * Cálculo y graficación del espectro en frecuencia de cualquier canal seleccionado.
  * Ventanas de ponderación temporal configurables (Hanning, Hamming, Blackman, Rectangular).
  * Detección y rotulado automático de picos de frecuencia predominantes y cálculo de densidad espectral de potencia.
* **Exportación de Datos:**
  * Generación de archivos tabulares `.csv` con estampas de tiempo en segundos, fecha UTC y columnas para cada parámetro.
  * Exportación directa a formato matricial binario de **MATLAB (`.mat`)**.
* **Consola de Configuración Serie:**
  * Comunicación directa con el FDR a través del puerto COM de mantenimiento (UART RS-232).
  * Comandos de consulta (`CMD 0x03`) y actualización en memoria NVS (`CMD 0x02`) del formato CEV de vuelo sin requerir consola de comandos de texto.

---

## Ejecución en Campo (Hangar / Ensayos en Vuelo)

La carpeta incluye el ejecutable autónomo para sistemas Windows de 64 bits:

```
FDR_Converter/dist/FDR_Converter.exe
```

1. Copiar `FDR_Converter.exe` a la computadora de servicio o directamente en el pendrive junto con los registros `.bin` descargados del FDR.
2. Ejecutar con doble clic (no requiere instalación previa de Python, librerías ni conexión a internet).
3. Seleccionar el archivo binario con el botón **Examinar...**.
4. Presionar **Convertir y Graficar**.
5. Para guardar los datos tabulados, hacer clic en **Guardar CSV** o **Exportar a MATLAB (.mat)**.

---

## Mapeo de Parámetros Exportados

| Canal / Columna | Descripción / Fuente | Unidad |
|-----------------|----------------------|:------:|
| `ciclo` | Número de ciclo mayor CEV | Entero |
| `tiempo_s` | Tiempo transcurrido desde el inicio del archivo | Segundos (s) |
| `ANA_1` a `ANA_16` | Canales analógicos rápidos (AD7606 x2) | LSB / Voltios |
| `ANA_17` a `ANA_32` | Canales analógicos lentos (AD7490) | LSB / Voltios |
| `DIG_1` a `DIG_16` | Entradas discretas de cabina (TCA9555) | 0 / 1 lógico |
| `DSP_1` a `DSP_3` | Aceleración lineal triaxial $A_x, A_y, A_z$ (IMU Xsens) | $m/s^2$ |
| `DSP_4` a `DSP_6` | Velocidades angulares $\omega_x, \omega_y, \omega_z$ (IMU Xsens) | $^{\circ}/s$ |
| `DSP_7` a `DSP_9` | Magnetómetro triaxial $M_x, M_y, M_z$ (IMU Xsens) | a.u. |
| `DSP_10`, `DSP_11` | Ángulos de actitud: Alabeo (Roll) y Cabeceo (Pitch) | Grados ($^{\circ}$) |
| `DSP_12` | Señal de sincronismo de pulso satelital PPS | 0 / 1 lógico |
| `DSP_13` | Ángulo de guiñada (Yaw / Heading) | Grados ($^{\circ}$) |
| `HOR`, `MIN`, `SEG` | Estampa de tiempo satelital UTC (GNSS) | Horas / Min / Seg |
| `DIA`, `MES`, `ANO` | Fecha satelital UTC (GNSS) | Día / Mes / Año |
| `LAT`, `LON`, `ALT` | Posición geográfica reconstruida (GNSS) | Grados / Pies |

---

## Generación del Ejecutable (`.exe`)

Si se realizan modificaciones sobre el script [fdr_converter.py](file:///C:/Users/yo/Documents/ESP/FDR/FDR_Converter/fdr_converter.py), el ejecutable puede recompilarse con PyInstaller:

```powershell
cd FDR_Converter
pip install -r requirements.txt
pyinstaller FDR_Converter.spec
```

El nuevo binario se generará automáticamente en el subdirectorio `dist/`.
