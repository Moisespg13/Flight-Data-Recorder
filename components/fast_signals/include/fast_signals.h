
/**
 * @file fast_signals.h
 * @brief API pública del driver AD7606 (Señales Rápidas).
 *
 * Define las estructuras de configuración, el handle opaco y las
 * funciones para inicializar, resetear, disparar conversión y leer
 * los 8 (por ahora) canales analógicos de alta velocidad (16 bits, >= 3kSPS).
 */

#pragma once

#include "driver/spi_master.h" // IWYU pragma: keep
#include "esp_err.h"           // IWYU pragma: keep
#include "hal/spi_types.h"     // IWYU pragma: keep

// Configuración para la adquisición de Señales Rápidas (AD7606)
typedef struct {
  spi_host_device_t spi_host; // Host SPI (ej: SPI2_HOST)
  int pin_cs;                 // Pin Chip Select
  int pin_convst;             // Pin Conversion Start (compartido o individual)
  int pin_busy;               // Pin Busy
  int pin_rst;                // Pin Reset (compartido)
} fast_signals_config_t;

// Handle para el driver
typedef struct fast_signals_dev_t *fast_signals_handle_t;

/**
 * @brief Inicializa el driver para Señales Rápidas (AD7606)
 *
 * @param config Puntero a la configuración
 * @param ret_handle Puntero para devolver el handle creado
 * @return esp_err_t ESP_OK si fue exitoso
 */
esp_err_t fast_signals_init(const fast_signals_config_t *config,
                            fast_signals_handle_t *ret_handle);

/**
 * @brief Lee los 8 canales de señales rápidas (16 bits c/u)
 *
 * @param handle Handle del dispositivo
 * @param data_buffer Buffer de al menos 8 * sizeof(int16_t)
 * @return esp_err_t ESP_OK si la lectura fue exitosa
 */
esp_err_t fast_signals_read(fast_signals_handle_t handle, int16_t *data_buffer);

/**
 * @brief Inicia la conversión (Dispara pin CONVST)
 *
 * @param handle Handle del dispositivo
 * @return esp_err_t
 */
esp_err_t fast_signals_start_conversion(fast_signals_handle_t handle);

/**
 * @brief Resetea el dispositivo de adquisición (Pulso en pin RESET)
 *
 * @param handle Handle del dispositivo
 * @return esp_err_t
 */
esp_err_t fast_signals_reset(fast_signals_handle_t handle);
