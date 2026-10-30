/**
 * @file rtc_ds3232.h
 * @brief Driver para Reloj de Tiempo Real (RTC) Maxim DS3232 / DS3231 por bus I2C.
 *
 * Provee funciones para inicializar el RTC externo, leer la fecha y hora UTC
 * durante el arranque en frío (previo al enganche del receptor GNSS) y recalibrar
 * el RTC cuando se dispone de hora satelital precisa.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Estructura de fecha y hora del RTC DS3232.
 */
typedef struct {
  uint16_t year;   /**< Año completo (ej: 2026) */
  uint8_t month;   /**< Mes (1-12) */
  uint8_t day;     /**< Día del mes (1-31) */
  uint8_t hour;    /**< Hora formato 24h (0-23) */
  uint8_t minute;  /**< Minutos (0-59) */
  uint8_t second;  /**< Segundos (0-59) */
  bool valid;      /**< true si la lectura fue exitosa */
} rtc_time_t;

/**
 * @brief Inicializa y comprueba la presencia del RTC DS3232 en el bus I2C.
 *
 * Utiliza los mismos pines I2C del sistema (I2C_NUM, PIN_I2C_SCL, PIN_I2C_SDA)
 * en la dirección esclava 0x68.
 *
 * @return ESP_OK si el RTC respondió correctamente.
 *         ESP_ERR_NOT_FOUND o código de error I2C si no está presente.
 */
esp_err_t rtc_ds3232_init(void);

/**
 * @brief Consulta si el RTC fue detectado y se encuentra disponible.
 *
 * @return true si el RTC está operativo.
 */
bool rtc_ds3232_is_available(void);

/**
 * @brief Lee la fecha y hora actual del RTC DS3232.
 *
 * @param time Puntero a la estructura donde se guardará la fecha/hora.
 * @return ESP_OK si la lectura I2C fue exitosa y los datos son coherentes.
 */
esp_err_t rtc_ds3232_get_time(rtc_time_t *time);

/**
 * @brief Ajusta la fecha y hora del RTC DS3232 (calibración satelital).
 *
 * Se utiliza para disciplinar el RTC con la hora atómica UTC provista por el GNSS.
 *
 * @param day Día del mes (1-31).
 * @param month Mes (1-12).
 * @param year Año completo (ej: 2026).
 * @param hour Hora en formato 24h (0-23).
 * @param minute Minutos (0-59).
 * @param second Segundos (0-59).
 * @return ESP_OK si la escritura fue confirmada por el RTC.
 */
esp_err_t rtc_ds3232_set_time(uint8_t day, uint8_t month, uint16_t year,
                              uint8_t hour, uint8_t minute, uint8_t second);

#ifdef __cplusplus
}
#endif
