# **Documento de Requisitos de Software (RS) — FDR Firmware v0.9**

> **Nota de implementación (septiembre 2026).** Este RS es la línea base v0.9. El firmware no incorpora RTC DS3232, ADS1115, MAX22190, eMMC ni USB-CDC como camino de descarga. La hora sale del GNSS y de un offset de PPS en el RP2040. Las lentas son un AD7490 (~20 Hz). Las discretas son 16 entradas en un TCA9555 leído por el maestro a 20 Hz, con debounce por software. El enlace inter-placa es SPI a 10 MHz con CRC32, esclavo por PIO. Detalle en `fdr_system_architecture.md`.


## **1\. Introducción**

### **1.1 Propósito**

Definir en forma completa y verificable los requisitos del firmware del FDR (Flight Data Recorder) para su diseño, implementación, verificación en tierra y validación en vuelo, bajo enfoque **V-Model**. Este SRS será la base contractual/técnica entre desarrollo (IUA) y usuario operativo (CEV-FAA).

### **1.2 Alcance**

Firmware de la **Placa A (ESP32-S3) y Placa B (RP2040)**: adquisición de señales analógicas/discretas, sincronización temporal (RTC+GNSS), almacenamiento robusto (SD/eMMC), interfaz de control (USB-CDC, opcional RF en tierra) y servicios de diagnóstico/salud. No incluye mecánica, arneses ni dashboards PC (se tratan en documentos aparte).

### **1.3 Definiciones y acrónimos**

* **FDR**: Flight Data Recorder

* **PPS**: Pulse Per Second (GNSS)

* **RTC**: Real-Time Clock

* **HAL**: Hardware Abstraction Layer

* **TLV**: Type-Length-Value (formato binario de registro)

* **FR**: Functional Requirement; **NFR**: Non-Functional Requirement

* **IVV**: Integración, Verificación y Validación

### **1.4 Referencias**

* Plano/diagrama en bloques del prototipo (Placa A).

* Políticas internas de verificación IUA/CEV.

* Manuales de dispositivos: ESP32-S3, ADS1115, AD7606, MAX22190, DS3232.

## **2\. Descripción general**

### **2.1 Perspectiva del producto**

* Unidad embebida que registra señales de vuelo en aeronaves del CEV-FAA.

* Interfaz con sensores/ADC, GNSS y almacenamiento removible/embebido.

* Funciona autónomamente en misión y permite descarga/control en tierra.

### **2.2 Usuarios**

* **Pilotos/ingenieros de ensayo (CEV)**: operación, inicio/parada de registro, marcado de eventos.

* **Técnicos de laboratorio**: preparación de misión, mantenimiento, calibración.

* **Equipo académico (IUA)**: análisis y docencia con datos reales.

### **2.3 Suposiciones y dependencias**

* Disponibilidad de energía estable del avión (rango especificado por HW).

* Señal GNSS con PPS en ensayos nominales; degradación controlada cuando no hay PPS.

* Uso de microSD industriales y/o eMMC ≥16–32 GB. (Redundancia)

### **2.4 Restricciones**

* Tiempo real blando con **latencias acotadas**.

* Robustez frente a EMC; tolerancia a power-loss.

* Código en C/C++ con **FreeRTOS (ESP-IDF)**; memoria pre-asignada (sin malloc en misión).

## **3\. Requisitos funcionales (FR)**

Numeración estable; cada requisito tiene método de verificación (V: Test, Análisis, Inspección, Demostración).

### **3.1 Adquisición analógica**

* **FR-A1.** El sistema deberá adquirir **16 canales rápidos** a **≥3 kSPS por canal**, con muestreo simultáneo. (V: Test)

* **FR-A2.** El sistema deberá adquirir **16 canales lentos** entre **8–128 SPS** (configurable), publicando un **frame a 20 Hz**. (V: Test)

* **FR-A3.** Guardar valores **en LSB** con metadatos de ganancia/FSR por canal para reconstrucción a Volt. (V: Inspección/Test)

### **3.2 Entradas discretas**

* **FR-D1.** Leer **16 entradas discretas** a **20 Hz**, con **debounce HW+SW** . (V: Test)

* **FR-D2.** Registrar cambios de estado con timestamp a nivel de frame. (V: Test)

### **3.3 Sincronización temporal**

* **FR-T1.** Integrar **RTC (DS3232)** como reloj base; **GNSS** para hora y posición; **PPS** para disciplining. (V: Test/Análisis)

* **FR-T2.** Entregar **timestamp UTC en ns** en cada paquete (monotónico, sin retrocesos). (V: Test)

* **FR-T3.** Con PPS, el **jitter** de alineación entre segundos no deberá exceder **±100 µs**. (V: Test/Análisis)

* **FR-T4.** Modos degradados: sin PPS → RTC; sin RTC → reloj interno \+ flag de degradación. (V: Test)

### **3.4 Almacenamiento y formato de datos**

* **FR-S1.** Registrar en **SD/eMMC** en archivos **rotativos** (p. ej. 512 MB), con **TLV** por paquete y **CRC32** por TLV. (V: Inspección/Test)

* **FR-S2.** Mantener **índice ligero** cada N TLV para búsquedas rápidas. (V: Inspección/Test)

* **FR-S3.** Cerrar archivos limpiamente al comando **STOP** o transición a **LANDING**; ante power-loss, el sistema deberá **recuperar** el FS y preservar el máximo de datos. (V: Test)

### **3.5 Comunicación y control**

* **FR-C1.** Proveer **USB-CDC** con shell: `status`, `start`, `stop`, `mark <tag>`, `cfg get/set`, `ls`, `cat meta`. (V: Demostración)

* **FR-C2.** Telemetría básica a 1–5 Hz (tasas, temps, voltaje, dropped frames). (V: Test)

* **FR-C3.** RF (WiFi/BLE) **opcional** sólo para pruebas en tierra, desactivable. (V: Demostración)

### **3.6 Salud, seguridad y supervisión**

* **FR-H1.** Implementar **WDT** por núcleo y **supervisor** de liveness/latencias. (V: Test)

* **FR-H2.** Publicar **health record** a 1 Hz (voltaje, temp, heap, contadores de caída, FS estado). (V: Test)

* **FR-H3.** Crear **CRASH.LOG** con causa de reset inesperado en arranque siguiente. (V: Test)

### **3.7 Estados y modos operativos**

* **FR-M1.** FSM: **IDLE → ARMED → RECORDING → LANDING → SAFE**, con transiciones por comando/condición. (V: Demostración)

* **FR-M2.** En **RECORDING** sólo una tarea escribe al FS; la adquisición nunca se bloquea por telemetría/comandos (back-pressure). (V: Test/Análisis)

---

## **4\. Requisitos no funcionales (NFR)**

### **4.1 Rendimiento**

* **NFR-P1.** **Latencia** desde captura rápida hasta encolado del bloque: **\< 2 ms** (p95). (V: Test)

* **NFR-P2.** **Pérdida de datos \= 0** en operación nominal 30 min; si ocurre back-pressure, se deberán **contabilizar** drops sin afectar continuidad. (V: Test)

* **NFR-P3.** Throughput de escritura sostenido compatible con tasas configuradas (margen ≥30 %). (V: Test)

### **4.2 Fiabilidad y robustez**

* **NFR-R1.** Recuperación tras **power-loss** sin corrupción masiva (sólo último archivo podría requerir reparación). (V: Test)

* **NFR-R2.** Operación con CPU promedio **\< 70 %** y watermark de stack **\> 25 %** en tareas críticas. (V: Test)

### **4.3 Mantenibilidad**

* **NFR-M1.** Código modular por componentes (HAL/middleware/app) con **Doxygen** y **estilo consistente**. (V: Inspección)

* **NFR-M2.** Pruebas unitarias con cobertura **\> 80 %** en funciones críticas. (V: Análisis/Test)

### **4.4 Seguridad de datos**

* **NFR-S1.** CRC32 por TLV; contador de secuencia por tipo; metadatos de configuración en header con CRC. (V: Inspección/Test)

---

## **5\. Interfaces externas**

* **HW**: \[ADS1115 (I²C)\] AD4131-8(SPI), AD7606 (SPI+DMA), MAX22190 (SPI con CRC), DS3232 (I²C), GNSS (UART \+ PPS), SD/eMMC (SPI/SDMMC), USB-CDC.

* **SW**: API de colas FreeRTOS para `q_fast`, `q_slow`, `q_discrete`, `q_write`; servicio `time_sync_now()` thread-safe.

---

## **6\. Restricciones y normas de diseño**

* FreeRTOS, tareas con prioridades definidas, ISR mínima (notificaciones/colas desde ISR).

* Sin **malloc** dinámico en misión; buffers pre-asignados.

* Sólo **tsk\_writer** accede al FS.

---

## **7\. Gestión de datos y formato de archivo**

* **Header**: versión, fecha, configuración, huella de build, CRC header.

* **TLVs**: `0x01 fast_block`, `0x02 slow_frame`, `0x03 discrete_frame`, `0x10 gnss_fix`, `0x20 health`, cada uno con `seq`, `ts_ns`, `len`, `payload`, `crc32`.

* **Índice**: offset cada N TLV para saltos rápidos.

* **Rollover**: por tamaño o tiempo, configurable.

---

## **8\. Estados y máquina de estados (resumen)**

* **IDLE**: sin registro; chequeos previos.

* **ARMED**: FS ok, espacio, hora válida; listo.

* **RECORDING**: adquisición y escritura activas.

* **LANDING**: flush final, cierre limpio, sellado de archivo.

* **SAFE**: listo para power-off.

---

## **9\. Verificación y validación (resumen)**

* **Unitarias** por módulo (HAL, time\_sync, storage, comm, health).

* **Integración**: tasas/latencias/colas, estrés de I/O.

* **Tierra**: coherencia temporal, power-loss, throughput, recovery.

* **Vuelo**: secuencias normalizadas, comparación con hipótesis (precisión y latencia).

---

# **Matriz de Trazabilidad Requisito ↔ Prueba (IVV)**

Tabla base (se amplía en el plan de pruebas).  
 Métodos: **T**\=Test, **A**\=Análisis, **I**\=Inspección, **D**\=Demostración.  
 Nivel: **U**\=Unitario, **I**\=Integración, **G**\=Tierra (Ground), **F**\=Vuelo.

| ID Req | Descripción corta | Verif. | Nivel | Test ID / Procedimiento | Aceptación |
| ----- | ----- | ----- | ----- | ----- | ----- |
| **FR-A1** | 16ch rápidos ≥3 kSPS/ch simultáneo | T | I,G | T-A1-Rápidos-Rate-Simul | Sin underrun ≥10 min |
| **FR-A2** | 16ch lentos, frame 20 Hz | T | I | T-A2-Lentos-20Hz | ±1 % periodo; máscara válida |
| **FR-A3** | Guardado en LSB \+ metadatos | I,T | U,I | T-A3-Metadatos-Reconstr | Reconstrucción V ±1 LSB |
| **FR-D1** | 16 discretas a 20 Hz (debounce/CRC) | T | I | T-D1-Discretas-CRC-Deb | Sin falsos; detecta cable |
| **FR-D2** | Timestamp en cambios | T | I | T-D2-Edge-TS | Δt coherente ±1 frame |
| **FR-T1** | RTC+GNSS+PPS integrados | T,A | I | T-T1-PPS-Lock | PPS lock estable |
| **FR-T2** | Timestamp UTC ns monotónico | T | I | T-T2-Monotonicidad | Sin retrocesos, ns válido |
| **FR-T3** | Jitter PPS ±100 µs | T,A | I,G | T-T3-Jitter-PPS | p95 ≤100 µs |
| **FR-T4** | Modos degradados tiempo | T | I | T-T4-Degradación | Flags correctos; drift log |
| **FR-S1** | TLV+CRC32+rotación archivos | I,T | I,G | T-S1-TLV-Rotación | CRC ok; rollover ok |
| **FR-S2** | Índice ligero | I,T | I | T-S2-Índice-Seek | Seek \<100 ms a N saltos |
| **FR-S3** | Cierre limpio / power-loss recovery | T | G | T-S3-PowerLoss | Recupera y monta FS |
| **FR-C1** | Shell USB comandos | D | I | P-C1-Shell-Cmds | Todos los comandos operan |
| **FR-C2** | Telemetría 1–5 Hz | T | I | T-C2-Telemetría-Rate | 1–5 Hz estable; no bloquea |
| **FR-C3** | RF opcional (tierra) | D | I | P-C3-RF-Ground | Habilita/inhabilita |
| **FR-H1** | WDT \+ supervisor | T | I | T-H1-WDT-Fault-Inject | No cuelga; reinicio controlado |
| **FR-H2** | Health record 1 Hz | T | I | T-H2-Health-Log | Campos completos y coherentes |
| **FR-H3** | CRASH.LOG en reset inesperado | T | I | T-H3-Crash-Marker | Archivo creado con causa |
| **FR-M1** | FSM de misión | D | I | P-M1-FSM-Transiciones | Transiciones y guardas ok |
| **FR-M2** | Writer exclusivo; back-pressure | T,A | I | T-M2-BP-Stress | Adq. continua; counters ok |
| **NFR-P1** | Latencia \<2 ms p95 | T | I | T-P1-Latency-Prof | p95 \<2 ms |
| **NFR-P2** | 0 pérdidas nominal; contabiliza drops | T | G | T-P2-Loss-Zero/Count | 0 en nominal; contadores ok |
| **NFR-P3** | Throughput con margen ≥30 % | T | I | T-P3-IO-Margin | ≥30 % headroom |
| **NFR-R1** | Power-loss: sin corrupción masiva | T | G | T-R1-PL-Recovery | FS usable; último archivo reparable |
| **NFR-R2** | CPU\<70 %, stack\>25 % | T | I | T-R2-Profiling-Watermark | Cumple en estrés |
| **NFR-M1** | Modular+Doxygen+estilo | I | U | I-M1-CodeReview | Checklist aprobado |
| **NFR-M2** | Cobertura \>80 % críticos | A,T | U | A-M2-Coverage-Report | ≥80 % líneas/ramos |

**Notas operativas:**  
 • Los IDs de prueba (T-xxx / P-xxx / I-xxx / A-xxx) son plantillas; cada uno tendrá un **procedimiento** con setup, pasos, datos de prueba, criterios y registro de resultados.  
 • Nivel **F (Vuelo)** se agregará para FR que requieran entorno operacional (p. ej., validaciones de correlación con parámetros de cabina).

