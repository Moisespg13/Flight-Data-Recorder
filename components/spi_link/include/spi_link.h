
/**
 * @file spi_link.h
 * @brief API pública del componente de enlace SPI inter-placa.
 *
 * Provee funciones para inicializar y arrancar la comunicación SPI
 * entre la placa Maestro y la placa Esclavo. La API no depende del
 * rol: internamente selecciona la tarea correcta según Kconfig.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/queue.h"

/**
 * @brief Inicializa el componente de enlace SPI inter-placa.
 *        Recibe la cola de datos a transmitir (Esclavo) o donde depositar
 *        datos recibidos (Maestro).
 *
 * @param q_tx_data Cola de datos (fast_data_payload_t) para TX (Esclavo)
 *                  o RX (Maestro). Puede ser NULL si no aplica.
 * @return esp_err_t ESP_OK si la inicialización fue exitosa.
 */
esp_err_t spi_link_init(QueueHandle_t q_tx_data);

/**
 * @brief Arranca la tarea de enlace SPI correspondiente al rol.
 *        - Esclavo: Tarea que consume la cola y transmite al Maestro.
 *        - Maestro: Tarea que solicita datos al Esclavo y valida CRC.
 *
 * @return esp_err_t ESP_OK si la tarea se creó correctamente.
 */
esp_err_t spi_link_start(void);
