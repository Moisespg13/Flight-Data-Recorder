# usb_transfer — Modo Descarga por USB Host (Clase MSC)

Componente que implementa el canal de extracción y descarga directa de los registros de vuelo en tierra sin necesidad de retirar físicamente las tarjetas MicroSD de la aeronave, utilizando el controlador USB-OTG nativo del ESP32-S3 operando como Host de Almacenamiento Masivo (Mass Storage Class - MSC).

---

## Interfaz de Hardware

* **Periférico:** Controlador USB-OTG Full-Speed (12 Mbps) integrado en el SoC ESP32-S3.
* **Componente Oficial:** `espressif__usb_host_msc`.

| Señal | GPIO | Descripción |
|-------|:----:|-------------|
| **USB D-** | 19 | Línea de datos diferencial negativa (Full-Speed) |
| **USB D+** | 20 | Línea de datos diferencial positiva (Full-Speed) |

---

## Lógica de Detección y Secuencia de Descarga

Al energizar el registrador de vuelo:
1. **Ventana de Espera Inicial:** Durante la rutina de arranque en `main()`, el sistema inicializa la pila del controlador USB Host y aguarda unos segundos la enumeración de un dispositivo de almacenamiento flash (pendrive con formato FAT32).
2. **Detección Positiva $\rightarrow$ Modo Descarga:**
   * El sistema inhibe el arranque de las tareas de adquisición y ensamblado de vuelo.
   * Monta las tarjetas MicroSD internas (`/sd1` y `/sd2`) en modo de solo lectura para evitar alteraciones de datos.
   * Monta el volumen del pendrive USB (`/usb`).
   * Escanea el directorio raíz de las tarjetas y copia secuencialmente todos los archivos binarios existentes (`FDR_XXXX.bin`) hacia la memoria USB externa.
   * El LED indicador en GPIO 0 parpadea a alta velocidad (5 Hz) informando la transferencia en curso.
   * Al concluir la copia completa, sincroniza los sectores del pendrive, desmonta los sistemas de archivos de manera segura y detiene el host USB.
3. **Ausencia de Dispositivo $\rightarrow$ Modo Vuelo:**
   * Si no se detecta la inserción de un pendrive transcurrido el tiempo límite de espera, la pila USB Host se desinicializa por completo para liberar memoria SRAM interna y el sistema continúa inmediatamente hacia la inicialización del Modo Vuelo normal.
