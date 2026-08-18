# FDR Converter — Manual de Uso
### Centro de Ensayo en Vuelo (CEV-FAA) / IUA

---

## ¿Qué es este software?

FDR Converter es la herramienta de software del sistema MAD (Módulo de Adquisición de Datos).
Cumple con las especificaciones CEV para Software 1 (Configuración y Calibración) y Software 2 (Explotación post-vuelo).

Funciona **completamente offline** — no requiere internet ni instalación adicional en la PC del hangar.

---

## Herramientas disponibles

### 📈 Software 2 — Explotación post-vuelo

| Pestaña | Qué muestra |
|---|---|
| **Señales Analógicas** | Los 32 canales ANA en función del tiempo. Click en la leyenda para mostrar/ocultar canales individuales. |
| **IMU / Actitud** | Acelerómetros (X/Y/Z), giróscopo, magnetómetro, roll y pitch. |
| **Señales Discretas** | Los 16 canales digitales (0/1) en función del tiempo. |
| **Tiempo GNSS** | Hora, minuto y segundo sincronizados por GPS. |
| **🔍 Vista Individual** | Seleccionás cualquier canal y lo ves solo con estadísticas (mín, máx, media, σ). |
| **📊 PSD / Vibración** | Densidad Espectral de Potencia por método de Welch. Grafica aceleración en **G²/Hz vs Hz** (spec 7.2 CEV). Permite seleccionar canal y rango de tiempo. |

### ⚙ Software 1 — Configuración y Calibración

| Panel | Qué hace |
|---|---|
| **Grilla de muestreo** | Define nombre, sensor/transductor, tasa de muestreo (rápido/lento) y rango de cada canal ANA. Permite seleccionar el formato CEV y bit rate (F1/F2/F3). |
| **Mínimos cuadrados** | Carga una tabla de puntos (valor LSB medido vs valor real de referencia) y calcula los coeficientes de calibración por ajuste polinomial. Muestra la curva ajustada y el coeficiente R². |

### Otras funciones

| Botón | Función |
|---|---|
| **🔄 Detectar dispositivo** | Escanea automáticamente pendrives y lectores SD conectados en busca de archivos `FDR_XXXX.bin`. |
| **🔧 Calibración de canales** | Permite ingresar o editar manualmente los coeficientes de calibración por canal. Soporta calibración lineal (`y = a·x + b`) y polinomial. Guarda/carga en archivo JSON. |
| **Mostrar valores calibrados** | Alterna entre valores crudos en LSB y valores en unidades físicas reales (kPa, °C, etc.) en todos los gráficos y en el CSV exportado. |
| **🖼 Exportar gráfico activo** | Guarda la pestaña visible como imagen PNG, PDF o SVG. |
| **📄 Generar reporte PDF** | Genera un PDF completo con portada (info del vuelo, fecha) y todos los gráficos. Listo para adjuntar en informes técnicos. |
| **💾 Guardar CSV** | Exporta todos los datos del vuelo a un archivo `.csv` compatible con Excel. |

---

## Pasos básicos para el hangar

### Paso 1 — Abrir el software

Doble click en `FDR_Converter.exe`
*(o en caso de usar Python: abrir PowerShell en la carpeta y ejecutar `python fdr_converter.py`)*

---

### Paso 2 — Cargar el archivo de vuelo

**Opción A — Pendrive o lector SD (recomendado):**
1. Conectar el dispositivo a la PC
2. Click en **"🔄 Detectar dispositivo"**
3. Doble click en el archivo `FDR_XXXX.bin` que aparece en la lista

**Opción B — Archivo manual:**
1. Click en **"…"** al lado de *Archivo .bin*
2. Navegar y seleccionar el archivo

> **Nota:** Si el archivo fue grabado con firmware anterior (sin header), aparece un diálogo pidiendo que se seleccione el formato CEV (F1, F2 o F3) manualmente.

---

### Paso 3 — Convertir y graficar

Click en **"▶ Convertir y Graficar"**

El CSV se guarda automáticamente en la misma carpeta que el `.bin`.
La barra de estado inferior muestra el progreso y el resumen del vuelo al terminar.

---

### Paso 4 — Explorar los datos

- Navegar entre las pestañas para ver cada tipo de señal
- En **Señales Analógicas** e **IMU**: hacer click en los nombres de la leyenda para mostrar u ocultar canales
- En **🔍 Vista Individual**: seleccionar cualquier canal del menú desplegable para verlo en detalle

---

### Paso 5 — Análisis de vibraciones (si aplica)

1. Ir a la pestaña **📊 PSD / Vibración**
2. Seleccionar el canal (para vibraciones: **DSP 1 = Acc X**, **DSP 2 = Acc Y**, **DSP 3 = Acc Z**)
3. Ingresar el rango de tiempo a analizar (`t inicio` y `t fin` en segundos)
4. Click en **"📊 Calcular PSD"**

El gráfico de **G²/Hz vs Hz** se genera automáticamente con el pico dominante marcado.

---

### Paso 6 — Generar el reporte

Click en **"📄 Generar reporte PDF"**, elegir dónde guardarlo.

El PDF incluye:
- Portada con nombre del archivo, formato CEV, duración del vuelo y fecha
- Todos los gráficos del vuelo
- El análisis PSD si fue calculado en esa sesión

---

## Formatos de archivo soportados

| Formato | Tramas | Words/trama | Ciclos/s | Uso típico |
|---|---|---|---|---|
| **F1** | 40 | 17 | 75 | Alta densidad de canales rápidos |
| **F2** | 40 | 11 | 75 | Densidad media |
| **F3** | 10 | 6  | 10 | Baja velocidad, vuelos largos |

---

## Archivos generados

| Archivo | Descripción |
|---|---|
| `FDR_XXXX.csv` | Datos completos del vuelo en formato tabla |
| `reporte_vuelo.pdf` | Informe con gráficos listo para entregar |
| `calibracion.json` | Coeficientes de calibración (reutilizable en vuelos futuros) |
| `config_grilla.json` | Configuración de canales (nombre, sensor, tasa) |

---

*FDR Converter — CEV-FAA / IUA*
