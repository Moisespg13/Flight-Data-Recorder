# **Especificación de Requisitos del Sistema – FDR (v1.0)**

> **Nota de implementación (septiembre 2026).** Este archivo es la línea base de requisitos. El firmware vigente se apartó en varios puntos: no hay eMMC ni RTC externo; la descarga es USB Host MSC a un pendrive; las analógicas lentas son un AD7490 a ~20 Hz; las 16 discretas las lee el maestro con un TCA9555. El estado real está en `fdr_system_architecture.md`.



## **1\. Alcance**

Registrador de datos de vuelo, modular, con arquitectura abierta, apto para ensayos del CEV-FAA.

## **2\. Definiciones**

**Requisitos del sistema:** especificaciones funcionales/no funcionales verificables que describen el comportamiento y restricciones del FDR.

## **3\. Requisitos Funcionales (RF)**

* **RF-01:** Adquirir los parámetros de vuelo a las FS especificadas.  
* **RF-02:**Almacenar datos en memoria microSD y eMMC de, al menos 32 GB.  
* **RF-03:** Sincronizar datos con tiempo absoluto RTC \+ GNSS (PPS).  
* **RF-04:** Descarga rápida por USB CDC (y WiFi opcional).  
* **RF-05:** Registrar  16 entradas discretas, 16 analógicas lentas y 16 analógicas rápidas.  
* **RF-06:** Generar archivos estructurados (binario \+ exportador CSV).

## **4\. Requisitos No Funcionales (RNF)**

* **RNF-01:** Operar en el rango **−40°C a \+85°C**.  
* **RNF-02:**  Alimentación del bus del avión.  
* **RNF-03:**  Usar Case PC/104.  
* **RNF-04:**  Mantener la integridad de datos **≥99.9%** (redundancia/CRC).  
* **RNF-05:**  Firmware modular bajo **RTOS**.  
* **RNF-06:** Compatibilidad electromagnética con aviónica.  
* **RNF-07:** Tolerante a vibraciones del vuelo.

## **5\. Seguridad/Normativa (RSN)**

* **RSN-01:** Protección entradas discretas hasta ±50 V; analógicas con TVS/limitación.  
* **RSN-02:** Registro de fallos y desconexión de sensores.  
* **RSN-03:** Cumplimiento con normativa y prácticas de diseño.  
* **RSN-04:** Implementar CRC, watchdogs y mecanismos de tolerancia a fallos.  
* **RSN-05:** Ensayos de calibración y verificación en laboratorio y en vuelo.

## **6\. Trazabilidad**

La relación entre los requisitos definidos en este documento y los componentes del sistema se detallan en el archivo “Matriz de Trazabilidad FDR”.

Este documento es complementario al presente y se mantiene sincronizado bajo el mismo esquema de versionado del proyecto.

## **7\. Versionado**

| Version | Cambios | Fecha | Responsable |
| :---- | :---- | :---- | :---- |
| V1.0 | Version original | 26/09/2025 | Martin Salamero |
| V1.01 | Correcciones menores de redacción y orden | 07/11/2025 | Pablo G. Miklosa |

