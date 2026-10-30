# discrete_signals — Entradas Discretas

Componente encargado del acondicionamiento, adquisición y filtrado digital de las 16 señales discretas de cabina y estado de aeronave (llaves, relés, indicadores de tren, presostatos) utilizando un expansor de puertos I2C **TCA9555**.

---

## Interfaz de Hardware

* **Controlador:** Semtech / Texas Instruments TCA9555 (expansor bidireccional I2C a 16 bits).
* **Periférico:** Bus I2C0 del ESP32-S3 a 400 kHz (Fast Mode).
* **Dirección I2C:** `0x20` (líneas de dirección A0, A1, A2 conectadas a GND).

| Señal | GPIO | Descripción |
|-------|:----:|-------------|
| **SCL** | 4 | Línea de reloj serie I2C |
| **SDA** | 5 | Línea de datos bidireccional I2C |

---

## Funcionamiento del Driver y Filtrado Antirrebote

1. **Frecuencia de Muestreo:** La tarea `tsk_discrete_master` consulta el expansor a una tasa periódica de **20 Hz** (cada 50 ms).
2. **Filtro Antirrebote por Software (Debounce):** Para eliminar transitorios causados por conmutación mecánica o interferencias inducidas en el cableado de la aeronave, el valor lógico de cada canal solo se da por válido cuando se reciben **3 lecturas consecutivas idénticas** (equivalente a una ventana de confirmación estable de 150 ms).
3. **Integración en la Trama CEV:** El estado consolidado de los 16 canales conforma una palabra binaria de 16 bits (`DIG`), la cual se almacena en el búfer compartido de parámetros y es insertada en la posición asignada de la sub-conmutación CEV (Word 17 en el Formato 1).
