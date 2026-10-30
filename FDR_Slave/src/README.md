# Módulos de Código Fuente de la Placa Esclava (RP2040)

Este directorio contiene la implementación en lenguaje C y ensamblador PIO del firmware esclavo del FDR, ejecutándose sobre el entorno multicore **FreeRTOS SMP** del microcontrolador **RP2040**.

---

## Índice de Archivos

1. [main.c](#1-mainc--punto-de-entrada-y-orquestación-freertos-smp)
2. [fast_signals.c / fast_signals.h](#2-fast_signalsc--driver-de-adquisición-rápida-ad7606)
3. [slow_signals.c / slow_signals.h](#3-slow_signalsc--driver-de-adquisición-lenta-ad7490)
4. [spi_link_slave.c / spi_link_slave.h](#4-spi_link_slavec--enlace-inter-placa-por-pio-y-dma)
5. [spi_slave_tx.pio](#5-spi_slave_txpio--máquina-de-estados-pio-para-esclavo-spi)

---

## 1. `main.c` — Punto de Entrada y Orquestación FreeRTOS SMP

Es el núcleo coordinador de la placa esclava. Se encarga de la configuración del sistema, la instanciación de recursos de sincronización de FreeRTOS, el ruteo de interrupciones unificadas de GPIO y la ejecución de las tareas concurrentes.

### Recursos Globales e Intercomunicación
* **`q_fast_data`:** Cola de mensajes para muestras rápidas (`fast_data_payload_t`). Profundidad: **64 elementos**.
* **`q_slow_data`:** Cola de mensajes para muestras lentas (`slow_data_payload_t`). Profundidad: **2 elementos**.
* **`spi0_mutex`:** Semáforo mutex binario que arbitra el acceso exclusivo al bus periférico SPI0 entre los convertidores rápidos (AD7606) y lentos (AD7490).

### Tareas Concurrentes

| Tarea | Prioridad | Función y Comportamiento |
|-------|:---------:|--------------------------|
| **`tsk_coordinator_slave`** | 8 | Tarea de máxima prioridad. Configura la interrupción por flanco de bajada de la línea de selección de chip `PIN_LINK_CS` (GP0). Permanece bloqueada hasta que la placa maestra (ESP32-S3) inicia la primera transacción SPI. Al recibir el flanco, notifica a todas las demás tareas del sistema para desbloquearlas y se autodestruye (`vTaskDelete(NULL)`), liberando su stack y memoria para el resto del vuelo. |
| **`tsk_acq_fast_slave`** | 7 | Disparada a una frecuencia nominal de **3003 Hz** (temporizador periódico repetitivo de hardware a **333 µs**). Emite el pulso `CONVST` mediante `fast_signals_start_conversion()`, lee los 16 canales de los dos integrados AD7606 con `fast_signals_read()`, asigna la marca de tiempo calibrada en nanosegundos (`get_calibrated_timestamp_ns()`) y encola la muestra en `q_fast_data`. Si la cola se llena por congestión de bus, incrementa el contador de descartes `s_fast_drops`. |
| **`tsk_acq_slow_slave`** | 6 | Ejecutada a una frecuencia fija de **20 Hz** (período de 50 ms gobernado por `vTaskDelayUntil`). Realiza la lectura secuencial de los 16 canales del AD7490 mediante `slow_signals_read()`, asigna el timestamp y deposita la muestra en `q_slow_data`. |
| **`tsk_spi_link_slave`** | 6 | Administra la transmisión hacia la placa maestra. Implementa una estrategia de drenado prioritario: mientras existan muestras en `q_fast_data`, las envía inmediatamente mediante `spi_link_slave_send_fast()`; cuando la cola rápida se vacía, extrae y transmite una muestra de `q_slow_data` para evitar inanición. Si ambas colas están vacías, se bloquea con espera pasiva. |
| **`tsk_time_sync_slave`** | 6 | Responsable del reloj disciplinado por GPS. Despertada por la interrupción de subida del pulso PPS (GP26). Calcula la discrepancia entre el tiempo transcurrido local (`delta_local_us`) y el segundo exacto de 1.000.000 µs de la referencia satelital, integrando la corrección en la variable `s_pps_offset_us`. |
| **`tsk_watchdog_slave`** | 1 | Tarea guardiana de mínima prioridad. Se ejecuta cada 500 ms invocando `watchdog_update()`. Si alguna tarea de mayor prioridad se cuelga en un lazo cerrado o no cede el procesador, esta tarea sufre inanición de CPU y el temporizador perro guardián reinicia el microcontrolador físicamente a los 2 segundos. |

### Rutina de Interrupción Compartida (`shared_gpio_callback`)
El Pico SDK solo admite un despachador central de interrupciones GPIO por núcleo de procesamiento. La función `shared_gpio_callback` clasifica los eventos por número de terminal:
* **`PIN_LINK_CS` (Flanco de bajada):** Despierta a `tsk_coordinator_slave` durante la secuencia de arranque.
* **`PIN_LINK_CS` (Flanco de subida):** Bajar inmediatamente la línea de handshake `PIN_DRDY` (GP3) para señalizar el fin de la transacción y desbloquea a `tsk_spi_link_slave`.
* **`PIN_PPS` (Flanco de subida):** Registra el contador de microsegundos de hardware (`time_us_64()`) con un retraso inferior a 1 µs y notifica a `tsk_time_sync_slave`.

---

## 2. `fast_signals.c` — Driver de Adquisición Rápida (AD7606)

Controla los dos convertidores analógico-digitales **AD7606** conectados en paralelo sobre el bus SPI0.

### Características Principales
* **Configuración del Bus:** SPI0 a **10 MHz**, longitud de palabra de **16 bits**, **Modo 2 (CPOL=1, CPHA=0)**.
* **Líneas Compartidas:** `PIN_ADC_CONVST` (GP10) para inicio conjunto de muestreo y `PIN_ADC_RST` (GP15) para reset.
* **Líneas Individuales:** `PIN_CS_AD7606_1` (GP5), `PIN_CS_AD7606_2` (GP27), `PIN_ADC_BUSY` (GP12) y `PIN_ADC2_BUSY` (GP28).

### Funciones Clave
* **`fast_signals_init(spi0_mutex)`:** Configura el periférico SPI0, asigna funciones a los pines, inicializa niveles inactivos de las líneas de control y ejecuta un pulso de reset por hardware.
* **`fast_signals_start_conversion()`:** Genera un pulso negativo de 1 µs en `CONVST`. Ambos chips retienen simultáneamente las 16 entradas analógicas e inician la conversión interna.
* **`wait_busy(chip_index)`:** Espera de forma activa mediante `busy_wait_us(1)` a que la línea BUSY descienda a cero (tiempo típico de conversión: ~4 µs, timeout: 1 ms).
* **`fast_signals_read(data_buffer)`:** Adquiere el `s_spi0_mutex` (timeout 2 ms), lee 8 palabras de 16 bits de chip 1 en `data_buffer[0..7]` activando su respectivo CS, lee 8 palabras del chip 2 en `data_buffer[8..15]` y libera el mutex.

---

## 3. `slow_signals.c` — Driver de Adquisición Lenta (AD7490)

Controla el convertidor analógico-digital de 16 canales **AD7490** que adquiere parámetros de variación lenta a 20 Hz sobre el mismo bus SPI0.

### Protocolo y Conmutación de Modo de Bus
El AD7490 utiliza un modo de sincronismo incompatible con el AD7606:
* Requiere **Modo 3 (CPOL=1, CPHA=1)** y palabras de 16 bits.
* La función `spi0_acquire_for_slow()` toma el mutex `s_spi0_mutex` (timeout de 50 ms) y reconfigura los registros de formato del bus mediante `spi_set_format()`.
* Al finalizar la lectura de los 16 canales, `spi0_release_for_slow()` restituye el bus a **Modo 2** y entrega el mutex.

### Manejo del Pipeline y Detección de Desincronización
El AD7490 opera bajo un esquema síncrono donde la conversión del canal programado en el ciclo $N$ se transmite durante la transacción del ciclo $N+1$:
1. **Trama de Cebado:** Antes de leer la secuencia, se emite una transacción dummy con el comando del canal 0 para alinear el pipeline interno del conversor.
2. **Lectura Secuencial:** En un lazo de 16 pasos, se transmite la palabra de control del canal $i+1$ mientras se lee el resultado del canal $i$.
3. **Validación de Dirección de Canal (`ADD3..ADD0`):** Los 4 bits más significativos devueltos por el integrado indican el canal que se acaba de convertir. El driver valida en cada muestra que `(rx_data >> 12) & 0x0F == canal_esperado`. Si se detecta una discrepancia (pipeline corrido por ruido o glitch de reloj), se incrementa `s_desync_count`, se descarta la ráfaga completa y la trama de cebado del siguiente ciclo restablece la sincronía automáticamente.

---

## 4. `spi_link_slave.c` — Enlace Inter-Placa por PIO y DMA

Implementa el protocolo de transporte hacia el ESP32-S3 a través de hardware programable PIO y acceso directo a memoria (DMA).

### Agrupamiento en Lotes (Batching)
Para que el ESP32-S3 no deba procesar 3000 interrupciones por segundo por el bus SPI, la función `spi_link_slave_send_fast()` acumula **4 muestras analógicas rápidas consecutivas** (`FAST_SAMPLES_PER_SPI_PKT = 4`) antes de disparar la transmisión. Esto conforma un paquete consolidado de:
* Encabezado común: 14 bytes (`fdr_header_t`).
* Carga útil: 4 muestras $\times$ 40 bytes = 160 bytes.
* Verificación: 4 bytes de CRC32.
* **Longitud total del paquete:** **178 bytes** transmitidos a una tasa neta de 750 Hz.

### Mecánica de Transmisión por DMA y Handshake
1. **Ensamblado del Paquete:** Se copia el payload en `tx_buffer`, se calcula la suma de verificación CRC32 y se expande el flujo a un búfer intermedio de 32 bits (`tx_buffer_32`) para que la instrucción PIO `out pins, 1` tome el bit 31 sin requerir alineaciones de sub-palabra.
2. **Disparo de DMA:** Se configura el canal DMA con transferencia de 32 bits hacia el FIFO del PIO (`&s_pio->txf[s_sm]`) con solicitud DREQ sincronizada al estado de la máquina.
3. **Elevación de DRDY:** Se coloca `PIN_DRDY` (GP3) en nivel alto (`1`), informando a la placa maestra que la información está lista en hardware.
4. **Espera Síncrona:** La tarea se suspende sin consumo de CPU mediante `ulTaskNotifyTakeIndexed(2, ...)` aguardando a que la maestra complete el reloj de lectura.
5. **Cierre de Handshake:** Al recibir el flanco ascendente de CS al finalizar la lectura, la interrupción baja inmediatamente `PIN_DRDY` a nivel bajo (`0`), completando el ciclo de transferencia de manera segura.

---

## 5. `spi_slave_tx.pio` — Máquina de Estados PIO para Esclavo SPI

Archivo de ensamblador para el coprocesador de Entrada/Salida Programable (PIO) del RP2040. Su función es emular un transmisor serie síncrono esclavo con respuesta de baja latencia.

### Código Ensamblador y Comportamiento

```pasm
.program spi_slave_tx
.define PUBLIC CS_GPIO  0
.define PUBLIC SCK_GPIO 2

.wrap_target
idle:
  wait 0 gpio CS_GPIO    ; Esperar a que la línea CS descienda a LOW
bitloop:
  jmp pin idle           ; Si CS vuelve a HIGH (fin o aborto de trama), volver a reposo
  wait 0 gpio SCK_GPIO   ; Aguardar flanco descendente del reloj SCK
  out pins, 1            ; Desplazar el siguiente bit hacia la línea MISO
  wait 1 gpio SCK_GPIO   ; Aguardar flanco ascendente de SCK (muestreo del maestro)
  jmp bitloop            ; Continuar con el siguiente bit
.wrap
```

### Ventajas del Enfoque PIO
* **Independencia de la CPU:** La transmisión de bits se produce de manera determinista a la velocidad del reloj de 10 MHz suministrado por el maestro, sin verse afectada por interrupciones o cambios de contexto del sistema operativo.
* **Gating por Selección de Chip (`CS`):** Si la placa maestra emite pulsos de reloj espurios o secuencias de sondeo con CS deseleccionado, el PIO permanece en estado de reposo y no consume bytes del FIFO ni desincroniza el paquete de datos estructurado.
