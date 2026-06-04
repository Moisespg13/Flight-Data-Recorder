
/**
 * @file sd_storage.h
 * @brief API del componente de almacenamiento en MicroSD.
 *
 * Maneja la inicialización del bus SDMMC, montaje del sistema de
 * archivos FATFS, creación de archivos de vuelo, escritura buffereada
 * y flush periódico para protección ante cortes de energía.
 *
 * Los datos se escriben en formato binario TLV: cada registro en el
 * archivo sigue la misma estructura que los paquetes inter-placa
 * (Header + Payload + CRC32), lo que permite reutilizar el mismo
 * parser para leer los archivos después del vuelo.
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Inicializa el bus SDMMC y monta el sistema de archivos FATFS.
 *
 * Configura los pines de la MicroSD, inicializa el driver SDMMC del
 * ESP32-S3 y monta la partición FAT en "/sdcard".
 *
 * @return ESP_OK si la tarjeta fue detectada y montada correctamente.
 */
esp_err_t sd_storage_init(void);

/**
 * @brief Crea (o abre) un nuevo archivo de vuelo en la MicroSD.
 *
 * Genera un nombre con patrón secuencial: FDR_0001.bin, FDR_0002.bin, etc.
 * Si el archivo ya existe, busca el siguiente número disponible.
 *
 * @return ESP_OK si el archivo fue creado/abierto correctamente.
 */
esp_err_t sd_storage_open_flight_file(uint8_t cev_format);

/**
 * @brief Escribe un bloque de datos al archivo de vuelo actual.
 *
 * Los datos se acumulan en un buffer interno. Cuando el buffer se
 * llena, se hace flush automático al archivo. También se puede
 * forzar el flush llamando a sd_storage_flush().
 *
 * @param data   Puntero a los datos a escribir.
 * @param length Cantidad de bytes a escribir.
 * @return ESP_OK si la escritura fue exitosa.
 */
esp_err_t sd_storage_write(const void *data, size_t length);

/**
 * @brief Fuerza la escritura de los datos pendientes en el buffer a disco.
 *
 * Llama a fwrite + fsync para garantizar que los datos lleguen
 * físicamente a la tarjeta y no queden en buffers del SO.
 *
 * @return ESP_OK si el flush fue exitoso.
 */
esp_err_t sd_storage_flush(void);

/**
 * @brief Cierra el archivo de vuelo actual y desmonta la tarjeta.
 *
 * Debe llamarse al finalizar la grabación (aterrizaje) para
 * asegurar la integridad del sistema de archivos FAT.
 */
void sd_storage_close(void);

/**
 * @brief Verifica el espacio libre en la MicroSD y libera si es necesario.
 *
 * Consulta el espacio libre mediante esp_vfs_fat_info(). Si es menor
 * al umbral definido (1 GB), borra secuencialmente los archivos
 * FDR_XXXX.bin más antiguos hasta alcanzar el espacio requerido.
 * Utiliza detección de wrap-around para mantener el orden cronológico.
 *
 * Debe llamarse ANTES de abrir un nuevo archivo de vuelo.
 *
 * @return ESP_OK si hay espacio suficiente (o se liberó con éxito).
 */
esp_err_t sd_storage_ensure_free_space(void);

/**
 * @brief Formatea la MicroSD completa (borra todos los archivos).
 *
 * Cierra cualquier archivo de vuelo abierto y ejecuta un formateo
 * FAT32 de la tarjeta. La tarjeta debe estar montada.
 *
 * @return ESP_OK si el formateo fue exitoso.
 */
esp_err_t sd_storage_format(void);
