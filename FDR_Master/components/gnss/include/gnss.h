
/**
 * @file gnss.h
 * @brief API del componente GNSS (GPS) para el Maestro.
 *
 * Maneja la comunicación UART con un módulo GNSS (u-blox NEO-M8N
 * o compatible), parsea las tramas NMEA para extraer hora UTC y
 * posición, y captura el pulso PPS por interrupción GPIO para
 * sincronización temporal de alta precisión.
 *
 * La señal PPS del módulo GPS se cablea físicamente a:
 *   - Maestro (ESP32, GPIO 37): Para timestamping preciso
 *   - Esclavo (RP2040, GP1):    Para corrección de drift
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Datos de tiempo UTC extraídos del GNSS.
 */
typedef struct {
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t day;
  uint8_t month;
  uint16_t year; // Año completo (ej: 2026)
  bool valid;    // true si el GPS tiene fix satelital
} gnss_time_t;

/**
 * @brief Datos de posición extraídos del GNSS.
 */
typedef struct {
  double latitude;    // Grados decimales (negativo = Sur)
  double longitude;   // Grados decimales (negativo = Oeste)
  float speed_knots;
  float altitude_m;   // Altitud MSL en metros (de $GPGGA)
  float hdop;         // Dilución horizontal de precisión
  uint8_t satellites; // Cantidad de satélites en uso
  bool valid;
} gnss_position_t;

/**
 * @brief Inicializa el driver UART y la interrupción PPS del GNSS.
 *
 * Configura UART2 a 9600 baud (estándar NMEA) y registra una
 * ISR en el pin PPS para capturar el instante exacto del pulso.
 *
 * @return ESP_OK si la inicialización fue exitosa.
 */
esp_err_t gnss_init(void);

/**
 * @brief Lee y parsea datos pendientes del GNSS (no bloqueante).
 *
 * Debe llamarse periódicamente desde una tarea. Lee los bytes
 * disponibles en el buffer UART y parsea las tramas NMEA completas.
 *
 * @return ESP_OK si se procesaron datos, ESP_ERR_NOT_FOUND si no hay datos.
 */
esp_err_t gnss_process(void);

/**
 * @brief Obtiene la hora UTC sincronizada con PPS.
 *
 * Combina la hora del último $GPRMC con los microsegundos
 * transcurridos desde el último pulso PPS para dar un timestamp
 * con precisión de microsegundos.
 *
 * @return Timestamp UTC en nanosegundos, o 0 si no hay fix.
 */
uint64_t gnss_get_utc_ns(void);

/**
 * @brief Obtiene la última hora UTC parseada del GNSS.
 * @param[out] time Estructura con hora, fecha y estado de validez.
 */
void gnss_get_time(gnss_time_t *time);

/**
 * @brief Obtiene la última posición parseada del GNSS.
 * @param[out] pos Estructura con lat, lon, velocidad y estado de validez.
 */
void gnss_get_position(gnss_position_t *pos);

/**
 * @brief Indica si el PPS ha sido recibido al menos una vez.
 * @return true si se detectó al menos un pulso PPS.
 */
bool gnss_pps_is_active(void);
