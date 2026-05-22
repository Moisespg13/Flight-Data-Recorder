
/**
 * @file gnss.c
 * @brief Driver GNSS con parser NMEA y captura PPS.
 *
 * Implementa la comunicación con un módulo GPS vía UART2 y la
 * captura del pulso PPS por interrupción GPIO para sincronización
 * temporal de alta precisión.
 *
 * Tramas NMEA parseadas:
 *   - $GPRMC / $GNRMC: Hora UTC, fecha, posición, velocidad, validez.
 *     Formato: $GPRMC,hhmmss.ss,A,llll.ll,N,yyyyy.yy,W,spd,crs,ddmmyy,...*hh
 *
 * Estrategia de sincronización:
 *   El PPS marca el borde exacto de cada segundo UTC. La trama NMEA
 *   llega ~200ms DESPUÉS del PPS y contiene la hora del segundo que
 *   ACABA de empezar con el PPS. Combinamos ambos para obtener
 *   timestamps con precisión de microsegundos:
 *
 *     UTC_ns = (hora_NMEA en ns) + (microsegundos desde último PPS) * 1000
 */

#include "gnss.h"
#include "pin_config.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "GNSS";

// UART configurado para el módulo GPS
// GNSS_UART_NUM viene de pin_config.h (centralizado)
#define GNSS_UART_BAUD   9600
#define GNSS_BUF_SIZE    512

// Buffer para acumular caracteres NMEA hasta encontrar '\n'
static char s_nmea_line[256];
static int s_nmea_pos = 0;

// Última hora y posición parseadas
static gnss_time_t s_time = {0};
static gnss_position_t s_position = {0};

// PPS: timestamp del último pulso (en microsegundos del timer interno)
static volatile int64_t s_pps_timestamp_us = 0;
static volatile bool s_pps_received = false;

// Hora base en nanosegundos (se actualiza con cada $GPRMC válido)
static volatile uint64_t s_utc_base_ns = 0;

// Flag: ¿ya recibimos al menos una corrección NMEA válida?
static volatile bool s_nmea_synced = false;

// ============================================================================
// ISR del PPS (Interrupción por flanco ascendente)
// ============================================================================
/**
 * @brief Interrupción que se dispara cuando el GPS sube el pin PPS.
 *
 * Captura el instante exacto del reloj interno del ESP32 (en µs).
 * Esta función corre en contexto de interrupción, por eso solo
 * guarda un número y no hace nada pesado (no puede llamar a printf,
 * no puede tomar mutexes, etc).
 */
static void IRAM_ATTR pps_isr_handler(void *arg) {
  s_pps_timestamp_us = esp_timer_get_time();
  s_pps_received = true;

  // Auto-incrementar la hora base en 1 segundo con cada PPS.
  // Esto evita el desfase de ~200ms que existe entre el PPS y
  // la llegada de la trama NMEA. La trama NMEA solo sirve como
  // corrección periódica (recalibración).
  if (s_nmea_synced) {
    s_utc_base_ns += 1000000000ULL; // +1 segundo
  }
}

// ============================================================================
// Parser NMEA
// ============================================================================

/**
 * @brief Convierte coordenada NMEA (grados+minutos) a grados decimales.
 *
 * NMEA usa formato ddmm.mmmm (grados * 100 + minutos decimales).
 * Ejemplo: 3442.1234 = 34 grados, 42.1234 minutos = 34.70206 grados
 */
static double nmea_to_degrees(const char *raw, const char *hemisphere) {
  if (raw == NULL || raw[0] == '\0') {
    return 0.0;
  }

  double val = atof(raw);
  int degrees = (int)(val / 100);
  double minutes = val - (degrees * 100);
  double result = degrees + (minutes / 60.0);

  // Sur y Oeste son negativos
  if (hemisphere != NULL && (*hemisphere == 'S' || *hemisphere == 'W')) {
    result = -result;
  }

  return result;
}

/**
 * @brief Extrae el campo N de una trama NMEA separada por comas.
 *
 * Modifica el string original (pone '\0' en las comas).
 * Devuelve un puntero al campo pedido, o NULL si no existe.
 */
static char *nmea_get_field(char *sentence, int index) {
  int count = 0;
  char *start = sentence;

  while (*start) {
    if (count == index) {
      // Buscar el final del campo (siguiente coma o fin de string)
      char *end = strchr(start, ',');
      if (end != NULL) {
        *end = '\0';
      }
      return start;
    }
    if (*start == ',') {
      count++;
    }
    start++;
  }

  return NULL;
}

/**
 * @brief Convierte hora y fecha NMEA a nanosegundos desde epoch Unix.
 *
 * Para simplificar (sin usar mktime que es pesado), calculamos
 * los nanosegundos del día (hh*3600 + mm*60 + ss) * 1e9.
 * El timestamp completo con fecha se puede calcular después si se necesita.
 */
static uint64_t time_to_day_ns(uint8_t h, uint8_t m, uint8_t s) {
  return ((uint64_t)h * 3600 + (uint64_t)m * 60 + (uint64_t)s) *
         1000000000ULL;
}

/**
 * @brief Parsea una trama $GPRMC o $GNRMC.
 *
 * Campos del $GPRMC:
 *   0: $GPRMC    (identificador)
 *   1: hhmmss.ss (hora UTC)
 *   2: A/V       (A=valid, V=void)
 *   3: llll.ll   (latitud)
 *   4: N/S
 *   5: yyyyy.yy  (longitud)
 *   6: E/W
 *   7: speed     (nudos)
 *   8: course
 *   9: ddmmyy    (fecha)
 */
static void parse_rmc(char *sentence) {
  // Hacer una copia de trabajo (nmea_get_field modifica el string)
  char work[256];
  strncpy(work, sentence, sizeof(work) - 1);
  work[sizeof(work) - 1] = '\0';

  // Extraer campos usando una copia limpia para cada uno
  // (reconstruimos el string cada vez porque get_field es destructivo)
  char copy[256];

  // Campo 1: Hora
  strncpy(copy, sentence, sizeof(copy) - 1);
  char *time_str = nmea_get_field(copy, 1);
  if (time_str == NULL || strlen(time_str) < 6) {
    return;
  }

  char hh[3] = {time_str[0], time_str[1], '\0'};
  char mm[3] = {time_str[2], time_str[3], '\0'};
  char ss[3] = {time_str[4], time_str[5], '\0'};

  // Campo 2: Validez
  strncpy(copy, sentence, sizeof(copy) - 1);
  char *status = nmea_get_field(copy, 2);
  bool valid = (status != NULL && *status == 'A');

  // Campo 9: Fecha
  strncpy(copy, sentence, sizeof(copy) - 1);
  char *date_str = nmea_get_field(copy, 9);

  // Campo 3-6: Posición
  strncpy(copy, sentence, sizeof(copy) - 1);
  char *lat_str = nmea_get_field(copy, 3);

  strncpy(copy, sentence, sizeof(copy) - 1);
  char *lat_hem = nmea_get_field(copy, 4);

  strncpy(copy, sentence, sizeof(copy) - 1);
  char *lon_str = nmea_get_field(copy, 5);

  strncpy(copy, sentence, sizeof(copy) - 1);
  char *lon_hem = nmea_get_field(copy, 6);

  // Campo 7: Velocidad
  strncpy(copy, sentence, sizeof(copy) - 1);
  char *spd_str = nmea_get_field(copy, 7);

  // Guardar hora
  s_time.hour = (uint8_t)atoi(hh);
  s_time.minute = (uint8_t)atoi(mm);
  s_time.second = (uint8_t)atoi(ss);
  s_time.valid = valid;

  // Guardar fecha (si disponible)
  if (date_str != NULL && strlen(date_str) >= 6) {
    char dd[3] = {date_str[0], date_str[1], '\0'};
    char mo[3] = {date_str[2], date_str[3], '\0'};
    char yy[3] = {date_str[4], date_str[5], '\0'};
    s_time.day = (uint8_t)atoi(dd);
    s_time.month = (uint8_t)atoi(mo);
    s_time.year = 2000 + (uint16_t)atoi(yy);
  }

  // Guardar posición
  if (valid) {
    s_position.latitude = nmea_to_degrees(lat_str, lat_hem);
    s_position.longitude = nmea_to_degrees(lon_str, lon_hem);
    s_position.speed_knots = (spd_str != NULL) ? (float)atof(spd_str) : 0.0f;
    s_position.valid = true;

    // Corregir la hora base con la referencia NMEA.
    // Esto recalibra el contador que el PPS auto-incrementa,
    // compensando cualquier drift acumulado.
    s_utc_base_ns = time_to_day_ns(s_time.hour, s_time.minute, s_time.second);
    s_nmea_synced = true;

    ESP_LOGD(TAG, "RMC: %02u:%02u:%02u %02u/%02u/%04u [%.6f, %.6f]",
             s_time.hour, s_time.minute, s_time.second, s_time.day,
             s_time.month, s_time.year, s_position.latitude,
             s_position.longitude);
  }
}

/**
 * @brief Procesa una línea NMEA completa.
 */
static void process_nmea_line(char *line) {
  // Solo nos interesan $GPRMC y $GNRMC (hora + posición)
  if (strncmp(line, "$GPRMC", 6) == 0 || strncmp(line, "$GNRMC", 6) == 0) {
    parse_rmc(line);
  }
  // TODO: Parsear $GPGGA para altitud y cantidad de satélites si se necesita
}

// ============================================================================
// API Pública
// ============================================================================

esp_err_t gnss_init(void) {
  ESP_LOGI(TAG, "Inicializando GNSS (UART%d, %d baud)...", GNSS_UART_NUM,
           GNSS_UART_BAUD);

  // Configurar UART para el módulo GPS
  uart_config_t uart_config = {
      .baud_rate = GNSS_UART_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t ret = uart_param_config(GNSS_UART_NUM, &uart_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar UART: %s", esp_err_to_name(ret));
    return ret;
  }

  // Asignar pines UART (RX del ESP32 = TX del GPS, y viceversa)
  ret = uart_set_pin(GNSS_UART_NUM, PIN_GNSS_TX, PIN_GNSS_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al asignar pines UART: %s", esp_err_to_name(ret));
    return ret;
  }

  // Instalar driver UART con buffer de recepción
  ret = uart_driver_install(GNSS_UART_NUM, GNSS_BUF_SIZE * 2, 0, 0, NULL, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al instalar driver UART: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "UART%d listo (RX=GPIO%d, TX=GPIO%d)", GNSS_UART_NUM,
           PIN_GNSS_RX, PIN_GNSS_TX);

  // Configurar pin PPS como entrada con interrupción por flanco ascendente
  gpio_config_t pps_conf = {
      .pin_bit_mask = (1ULL << PIN_PPS),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_POSEDGE, // Flanco ascendente
  };
  ret = gpio_config(&pps_conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar PPS: %s", esp_err_to_name(ret));
    return ret;
  }

  // Registrar la ISR del PPS
  ret = gpio_install_isr_service(0);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    // ESP_ERR_INVALID_STATE = ya estaba instalado (OK)
    ESP_LOGE(TAG, "Error al instalar servicio ISR: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = gpio_isr_handler_add(PIN_PPS, pps_isr_handler, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al registrar ISR del PPS: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "PPS configurado en GPIO%d (flanco ascendente)", PIN_PPS);
  ESP_LOGI(TAG, "GNSS inicializado. Esperando fix satelital...");

  return ESP_OK;
}

esp_err_t gnss_process(void) {
  // Leer bytes disponibles del UART (no bloqueante)
  uint8_t byte;
  int bytes_read = 0;

  while (uart_read_bytes(GNSS_UART_NUM, &byte, 1, 0) > 0) {
    bytes_read++;

    if (byte == '\n' || byte == '\r') {
      if (s_nmea_pos > 0) {
        s_nmea_line[s_nmea_pos] = '\0';
        process_nmea_line(s_nmea_line);
        s_nmea_pos = 0;
      }
    } else {
      if (s_nmea_pos < (int)(sizeof(s_nmea_line) - 1)) {
        s_nmea_line[s_nmea_pos++] = (char)byte;
      }
    }
  }

  return (bytes_read > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

uint64_t gnss_get_utc_ns(void) {
  if (!s_time.valid || !s_pps_received) {
    return 0; // Sin fix o sin PPS, no podemos dar hora confiable
  }

  // Calcular cuántos microsegundos pasaron desde el último PPS
  int64_t now_us = esp_timer_get_time();
  int64_t elapsed_us = now_us - s_pps_timestamp_us;

  // Si pasó más de 2 segundos desde el último PPS, el GPS se perdió
  if (elapsed_us < 0 || elapsed_us > 2000000) {
    return 0;
  }

  // UTC = hora_base (del último RMC) + microsegundos transcurridos
  return s_utc_base_ns + (uint64_t)elapsed_us * 1000;
}

void gnss_get_time(gnss_time_t *time) {
  if (time != NULL) {
    *time = s_time;
  }
}

void gnss_get_position(gnss_position_t *pos) {
  if (pos != NULL) {
    *pos = s_position;
  }
}

bool gnss_pps_is_active(void) { return s_pps_received; }
