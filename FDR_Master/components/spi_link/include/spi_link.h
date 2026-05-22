
/**
 * @file spi_link.h
 * @brief API pública del componente de enlace SPI inter-placa (lado Maestro).
 *
 * Provee funciones para inicializar y arrancar la comunicación SPI
 * con el Esclavo (RP2040). Recibe paquetes de datos rápidos y lentos,
 * valida CRC y los deposita en sus respectivas colas de recepción.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/**
 * @brief Inicializa el bus SPI y el componente de enlace.
 *
 * @param q_rx_fast Cola donde se depositarán los datos rápidos (fast_data_payload_t).
 * @param q_rx_slow Cola donde se depositarán los datos lentos (slow_data_payload_t).
 * @return esp_err_t ESP_OK si la inicialización fue exitosa.
 */
esp_err_t spi_link_init(QueueHandle_t q_rx_fast, QueueHandle_t q_rx_slow);

/**
 * @brief Arranca la tarea de polling SPI que lee datos del Esclavo.
 * @return esp_err_t ESP_OK si la tarea se creó correctamente.
 */
esp_err_t spi_link_start(void);
