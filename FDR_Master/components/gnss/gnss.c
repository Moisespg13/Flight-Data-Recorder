
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
 *   - $GPGGA / $GNGGA: Altitud MSL, satélites en uso, HDOP.
 *     Formato: $GPGGA,hhmmss.ss,llll.ll,N,yyyyy.yy,W,q,nn,hdop,alt,M,...*hh
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

// Instante del último fix válido, para poder invalidar la posición si el
// receptor deja de reportar. Sin esto, un GPS que muere a mitad de vuelo
// sigue publicando la última posición como si fuera buena.
static volatile int64_t s_last_fix_us = 0;

// Ventanas de validez
#define PPS_TIMEOUT_US 2000000 // Dos pulsos perdidos = sincronismo caído
#define FIX_TIMEOUT_US 3000000 // Tres tramas RMC perdidas = fix caído

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

#define NMEA_MAX_FIELDS 20

static char s_empty[] = "";

/**
 * @brief Parte una sentencia NMEA en campos terminados en '\0'.
 *
 * Copia la sentencia una sola vez a @p buf, reemplaza cada coma por '\0' y
 * deja en @p fields un puntero al inicio de cada campo. Los campos que la
 * sentencia no trae quedan apuntando a string vacío, de modo que el resto
 * del parser nunca tiene que comprobar NULL ni distinguir "campo ausente"
 * de "campo vacío".
 *
 * @return Cantidad de campos presentes en la sentencia.
 */
static int nmea_split(const char *sentence, char *buf, size_t buf_len,
                      char *fields[], int max_fields) {
  strncpy(buf, sentence, buf_len - 1);
  buf[buf_len - 1] = '\0';

  int n = 0;
  char *p = buf;
  fields[n++] = p;

  while (*p != '\0' && n < max_fields) {
    if (*p == ',') {
      *p = '\0';
      fields[n++] = p + 1;
    }
    p++;
  }

  // El último campo arrastra el checksum ("...*hh"): lo recortamos.
  char *star = strchr(fields[n - 1], '*');
  if (star != NULL) {
    *star = '\0';
  }

  for (int i = n; i < max_fields; i++) {
    fields[i] = s_empty;
  }

  return n;
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
static void parse_rmc(const char *sentence) {
  char buf[256];
  char *f[NMEA_MAX_FIELDS];

  // Necesitamos hasta el campo 9 (fecha) para considerar la trama utilizable.
  if (nmea_split(sentence, buf, sizeof(buf), f, NMEA_MAX_FIELDS) < 10) {
    return;
  }

  const char *time_str = f[1];
  if (strlen(time_str) < 6) {
    return;
  }

  char hh[3] = {time_str[0], time_str[1], '\0'};
  char mm[3] = {time_str[2], time_str[3], '\0'};
  char ss[3] = {time_str[4], time_str[5], '\0'};

  bool valid = (f[2][0] == 'A');

  const char *lat_str = f[3];
  const char *lat_hem = f[4];
  const char *lon_str = f[5];
  const char *lon_hem = f[6];
  const char *spd_str = f[7];
  const char *date_str = f[9];

  // Guardar hora
  s_time.hour = (uint8_t)atoi(hh);
  s_time.minute = (uint8_t)atoi(mm);
  s_time.second = (uint8_t)atoi(ss);
  s_time.valid = valid;

  // Guardar fecha (si disponible)
  if (strlen(date_str) >= 6) {
    char dd[3] = {date_str[0], date_str[1], '\0'};
    char mo[3] = {date_str[2], date_str[3], '\0'};
    char yy[3] = {date_str[4], date_str[5], '\0'};
    s_time.day = (uint8_t)atoi(dd);
    s_time.month = (uint8_t)atoi(mo);
    s_time.year = 2000 + (uint16_t)atoi(yy);
  }

  // Guardar posición
  if (!valid) {
    s_position.valid = false;
  } else {
    s_position.latitude = nmea_to_degrees(lat_str, lat_hem);
    s_position.longitude = nmea_to_degrees(lon_str, lon_hem);
    s_position.speed_knots = (float)atof(spd_str);
    s_position.valid = true;
    s_last_fix_us = esp_timer_get_time();

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
 * @brief Parsea una trama $GPGGA o $GNGGA.
 *
 * Campos del $GPGGA:
 *   0: $GPGGA    (identificador)
 *   1: hhmmss.ss (hora UTC — ya la tomamos del RMC, ignoramos acá)
 *   2: llll.ll   (latitud — ya la tomamos del RMC)
 *   3: N/S
 *   4: yyyyy.yy  (longitud — ya la tomamos del RMC)
 *   5: E/W
 *   6: quality   (0=sin fix, 1=GPS, 2=DGPS, etc.)
 *   7: nn        (cantidad de satélites en uso)
 *   8: hdop      (dilución horizontal de precisión)
 *   9: alt       (altitud sobre el nivel del mar, en metros)
 *  10: M         (unidad de altitud, siempre 'M')
 */
static void parse_gga(const char *sentence) {
  char buf[256];
  char *f[NMEA_MAX_FIELDS];

  // Necesitamos hasta el campo 9 (altitud).
  if (nmea_split(sentence, buf, sizeof(buf), f, NMEA_MAX_FIELDS) < 10) {
    return;
  }

  // Campo 6: Calidad del fix (0 = sin fix)
  if (f[6][0] == '0' || f[6][0] == '\0') {
    return; // Sin fix, descartar
  }

  if (f[7][0] != '\0') {
    s_position.satellites = (uint8_t)atoi(f[7]);
  }
  if (f[8][0] != '\0') {
    s_position.hdop = (float)atof(f[8]);
  }
  if (f[9][0] != '\0') {
    s_position.altitude_m = (float)atof(f[9]);
  }

  ESP_LOGD(TAG, "GGA: sats=%u hdop=%.1f alt=%.1fm",
           s_position.satellites, s_position.hdop, s_position.altitude_m);
}

/**
 * @brief Procesa una línea NMEA completa.
 */
static void process_nmea_line(const char *line) {
  // $GPRMC / $GNRMC: hora, fecha, posición, velocidad
  if (strncmp(line, "$GPRMC", 6) == 0 || strncmp(line, "$GNRMC", 6) == 0) {
    parse_rmc(line);
  }
  // $GPGGA / $GNGGA: altitud, satélites, HDOP
  else if (strncmp(line, "$GPGGA", 6) == 0 || strncmp(line, "$GNGGA", 6) == 0) {
    parse_gga(line);
  }
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

  // El servicio de ISR de GPIO lo instala master_logic_start() una única vez.
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
  if (elapsed_us < 0 || elapsed_us > PPS_TIMEOUT_US) {
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
  if (pos == NULL) {
    return;
  }

  *pos = s_position;

  // El fix se declara caído si hace rato que no llega una RMC válida, aunque
  // la última que llegó haya sido buena.
  if (pos->valid) {
    int64_t age_us = esp_timer_get_time() - s_last_fix_us;
    if (age_us < 0 || age_us > FIX_TIMEOUT_US) {
      pos->valid = false;
    }
  }
}

bool gnss_pps_is_active(void) {
  if (!s_pps_received) {
    return false;
  }

  // s_pps_received solo indica que alguna vez llegó un pulso. Lo que importa
  // es si el sincronismo sigue vivo ahora.
  int64_t age_us = esp_timer_get_time() - s_pps_timestamp_us;
  return (age_us >= 0 && age_us <= PPS_TIMEOUT_US);
}
