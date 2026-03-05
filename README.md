# Flight Data Recorder (FDR) - Firmware

Repositorio del firmware para el Registrador de Datos de Vuelo (FDR), un sistema diseñado para ensayos en vuelo del Centro de Ensayos en Vuelo (CEV-FAA) y desarrollado en el Instituto Universitario Aeronáutico (IUA).

## Descripción General
El sistema FDR está basado en la utilización de dos microcontroladores **ESP32-S3** bajo un entorno de tiempo real (FreeRTOS) y el framework ESP-IDF. El hardware está dividido físicamente en dos placas que se comunican mediante un bus SPI. La solución de emplear dos ESP32-S3 idénticos resolvió las limitaciones de periféricos SPI encontradas en diseños preliminares.

## Arquitectura de Hardware
El hardware se particiona funcionalmente en dos tarjetas principales:

*   **Placa A (MainBoard / Maestra):** Encargada del procesamiento principal, almacenamiento masivo y redundante (microSD y eMMC de al menos 32 GB), sincronización absoluta vía GNSS y RTC (DS3232), y descarga de datos/comandos por PC vía USB-CDC.
*   **Placa B (InputsBoard / Adquisición / Esclava):** Actúa como co-procesador dedicado a acondicionar y muestrear los sensores del avión sin bloquear la CPU principal.
    *   *Entradas Analógicas Rápidas:* 16 canales mediante 2 conversores AD7606 de 16 bits (muestreo simultáneo a $\ge 3$ kSPS).
    *   *Entradas Analógicas Lentas:* 16 canales mediante 1 conversor de ultra bajo consumo AD4131-8 (frecuencia de hasta 10 SPS).
    *   *Entradas Discretas:* 16 canales aislados utilizando serializadores MAX22190 con detección de corte de cable, protegidos por aisladores digitales MAX14483, muestreados a 20 Hz.

## Arquitectura de Software
El firmware está modularizado y gestionado por tareas concurrentes bajo FreeRTOS para garantizar la robustez, tiempos determinísticos y una latencia estricta menor a 2 ms desde la captura hasta el encolado.

### Selección de Rol (Kconfig)
Un único código fuente sirve para ambas placas. El rol se selecciona en tiempo de compilación utilizando las opciones nativas de `Kconfig` del Kernel (`CONFIG_FDR_ROLE_MASTER` o `CONFIG_FDR_ROLE_SLAVE`). El punto de entrada `app_main()` lee esta configuración y lanza la lógica correspondiente.

## Requisitos y Configuración de Desarrollo
*   **Framework:** Espressif IoT Development Framework (ESP-IDF).
*   **Lenguaje:** C/C++.
*   **IDE Recomendado:** Visual Studio Code.
*   **Gestión de Versiones:** Git y GitHub.

## Estándares y Normativa
Aunque el sistema fue concebido para vuelos de ensayo y no pretende una certificación civil comercial estricta, su firmware y hardware se desarrollan incorporando principios de normativas aeronáuticas como RTCA DO-178C (Desarrollo de Software), RTCA DO-160G (Entorno) y EUROCAE ED-112A (Crash Protected Recorders).

## Créditos
Desarrollado como proyecto de Trabajo Final de Grado e integrado al proyecto de extensión FDR en el Instituto Universitario Aeronáutico (IUA).
