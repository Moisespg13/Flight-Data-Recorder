
/**
 * @file master_logic.h
 * @brief API pública del componente de lógica del Maestro.
 *
 * Contiene master_logic_start() para arrancar la recepción de datos
 * del Esclavo (RP2040) y su procesamiento/almacenamiento, así como
 * la lectura de señales discretas y la señal de sincronización.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Arranca la lógica del Maestro.
 *        Configura SYNC_TRIG, inicializa discretas, crea colas de recepción,
 *        inicializa el enlace SPI, y lanza las tareas de procesamiento.
 *
 * @return esp_err_t ESP_OK si todo arrancó correctamente.
 */
esp_err_t master_logic_start(void);
