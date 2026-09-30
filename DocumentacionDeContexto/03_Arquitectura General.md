# **Arquitectura General (v1.2) – Dos Placas**

El detalle del firmware vigente está en `fdr_system_architecture.md`. Esta página conserva el particionado de placas y el historial.

## **Particionado**

**Placa A (Maestra — ESP32-S3):**

* Fuente de alimentación principal.
* Orquestación, ensamblado CEV y almacenamiento.
* GNSS (UART + PPS) e IMU Xsens (UART). No hay RTC externo: la hora sale del GNSS y, en el esclavo, de un offset disciplinado por PPS.
* 16 entradas discretas por expansor I2C **TCA9555** (dirección `0x20`).
* Dos MicroSD en SPI3 a 20 MHz (redundancia). No hay eMMC en el firmware.
* Descarga a pendrive por USB Host MSC, y puerto de comandos (RS-232 en la PCB).

**Placa B (Adquisición — RP2040-Zero):**

* 16 canales rápidos: 2× AD7606, muestreo simultáneo a 3 kHz.
* 16 canales lentos: 1× AD7490, barrido a ~20 Hz. Reemplaza al AD4131 de las revisiones anteriores.
* Enlace SPI esclavo hacia la placa A, implementado con PIO (el SPI de hardware del RP2040 no se usa en modo esclavo).

## **Bus inter-placas**

* SPI de un solo extremo a 10 MHz: SCK, MISO y CS, más la línea DRDY. El esclavo solo transmite.
* **PPS (1 Hz)** desde el GNSS de la placa A hacia GP26 del RP2040.
* El arranque coordinado es el primer flanco de bajada de CS. No hay línea `TRIG_START` / `SYNC_TRIG`.

## **Aislamiento y EMC**

* Dominios separados donde aplique; return path controlado; stitching vias; shield opcional.

## **Versionado**

| Version | Cambios | Fecha | Responsable |
| :---- | :---- | :---- | :---- |
| V1.0 | Version original | 26/09/2025 | Martin Salamero |
| V1.01 | Correcciones menores en la redacción. | 07/11/2025 | Pablo G. Miklosa |
| V1.1 | Muestreo de datos digitales ahora en **Placa A.** | 29/03/2026 | Ramsés E. Padrón |
| V1.2 | Alineado al firmware: AD7490, TCA9555, dual MicroSD por SPI, enlace PIO sin LVDS y sin `TRIG_START`. | 23/09/2026 | Ramsés E. Padrón |
