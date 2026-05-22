# **Arquitectura General (v1.1) – Dos Placas**

## **Particionado**

**Placa A (Maestra):**

*  Fuente de alimentación principal.  
*  Microcontrolador central.  
*  RTC/GNSS maestro (PPS).  
* Almacenamiento masivo (eMMC/microSD).  
* Puertos de comunicación para descarga de datos / ingreso de comandos.  
* Entradas discretas aisladas.

**Placa B (Adquisición)**: 

* ADCs de muestreo rápidos.  
* ADCs de muestreo lentos.  
* Microcontrolador secundario para lectura en tiempo real de las entradas.

## **Bus inter-placas**

* Unir las dos placas apiladas con **SPI** (SCK/MOSI/MISO/CS) pero **en pares diferenciales LVDS** a través del conector mezzanine.

* **PPS (1 Hz) desde Placa A:** Distribuye un pulso por segundo de muy baja variación → Ancla temporal absoluta.

* **TRIG\_START:** Pulso de arranque simultáneo para que **A y B** comiencen a muestrear a la vez.

## **Aislamiento y EMC**

* Dominios separados donde aplique; return path controlado; stitching vias; shield opcional.

## **5\. Versionado**

| Version | Cambios | Fecha | Responsable |
| :---- | :---- | :---- | :---- |
| V1.0 | Version original | 26/09/2025 | Martin Salamero |
| V1.01 | Correcciones menores en la redacción. | 07/11/2025 | Pablo G. Miklosa |
| V1.1 | Muestreo de datos digitales ahora en **Placa A.** | 29/03/2026 | Ramsés E. Padrón |

