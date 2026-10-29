
/**
 * @file usb_transfer.h
 * @brief API del módulo de transferencia USB Host (pendrive).
 *
 * Implementa la funcionalidad de USB Host MSC (Mass Storage Class)
 * para copiar archivos de vuelo desde la MicroSD a un pendrive.
 *
 * Flujo de operación:
 *   1. Técnico enchufa un pendrive al conector USB del MAD.
 *   2. El sistema detecta la inserción y monta el pendrive.
 *   3. Copia todos los archivos .bin de /sd1 a /usb.
 *   4. Desmonta el pendrive y señaliza que la descarga terminó.
 *
 * NOTA: Esta funcionalidad solo se activa en MODO DESCARGA.
 *       Durante el vuelo, el puerto USB no se inicializa.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

/**
 * @brief Modos de operación del MAD.
 */
typedef enum {
  MAD_MODE_FLIGHT,   // Modo vuelo: adquisición + grabación en SD
  MAD_MODE_DOWNLOAD  // Modo descarga: USB Host + copia a pendrive
} mad_mode_t;

/**
 * @brief Inicializa el stack USB Host y espera un pendrive.
 *
 * Configura el controlador USB-OTG del ESP32-S3 en modo Host,
 * instala el driver de clase MSC y monta el pendrive cuando
 * se detecta la conexión.
 *
 * @return ESP_OK si la inicialización fue exitosa.
 */
esp_err_t usb_transfer_init(void);

/**
 * @brief Copia todos los archivos de vuelo de la SD al pendrive.
 *
 * Recorre /sd1 buscando archivos FDR_XXXX.bin y los copia
 * al pendrive montado en /usb. Muestra progreso por log.
 *
 * @return ESP_OK si la copia fue exitosa.
 *         ESP_ERR_NOT_FOUND si no hay archivos para copiar.
 *         ESP_ERR_INVALID_STATE si el pendrive no está montado.
 */
esp_err_t usb_transfer_copy_all(void);

/**
 * @brief Desmonta el pendrive y libera recursos USB.
 * @return ESP_OK si la desmontó correctamente.
 */
esp_err_t usb_transfer_deinit(void);

/**
 * @brief Indica si hay un pendrive conectado y montado.
 * @return true si el pendrive está listo para escritura.
 */
bool usb_transfer_is_ready(void);
