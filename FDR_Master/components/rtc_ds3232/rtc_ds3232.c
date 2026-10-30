/**
 * @file rtc_ds3232.c
 * @brief Implementación del driver para RTC Maxim DS3232 / DS3231 por bus I2C.
 *
 * El DS3232 es un RTC de ultra-alta precisión con oscilador TCXO compensado
 * en temperatura y respaldo por batería (CR2032). Se comunica en modo esclavo I2C
 * en la dirección de 7 bits 0x68.
 *
 * Mapa de registros principales (formato BCD):
 *   - 0x00: Segundos (00-59)
 *   - 0x01: Minutos (00-59)
 *   - 0x02: Horas (00-23 en modo 24 horas, bit 6 = 0)
 *   - 0x03: Día de la semana (1-7)
 *   - 0x04: Día del mes / Fecha (01-31)
 *   - 0x05: Mes y Siglo (01-12, bit 7 = siglo)
 *   - 0x06: Año (00-99, relativo al año 2000)
 *   - 0x0E: Registro de Control
 *   - 0x0F: Registro de Estado (Bit 7: OSF - Oscillator Stop Flag)
 */

#include "rtc_ds3232.h"
#include "pin_config.h"

#include "driver/i2c.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "RTC_DS3232";

#define DS3232_I2C_ADDR       0x68
#define DS3232_REG_SECONDS    0x00
#define DS3232_REG_CONTROL    0x0E
#define DS3232_REG_STATUS     0x0F
#define DS3232_STATUS_OSF     0x80

static bool s_rtc_available = false;

// ============================================================================
// Funciones de Conversión BCD <-> Binario
// ============================================================================

static inline uint8_t bcd_to_dec(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static inline uint8_t dec_to_bcd(uint8_t dec) {
  return ((dec / 10) << 4) | (dec % 10);
}

// ============================================================================
// API Pública
// ============================================================================

esp_err_t rtc_ds3232_init(void) {
  s_rtc_available = false;

  // Garantizar que los parámetros del bus I2C estén configurados
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = PIN_I2C_SDA,
      .scl_io_num = PIN_I2C_SCL,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = 400000, // 400 kHz Fast Mode
  };
  i2c_param_config(I2C_NUM, &conf);
  // Si el driver ya fue instalado por discrete_signals, retorna ESP_ERR_INVALID_STATE (lo ignoramos)
  i2c_driver_install(I2C_NUM, conf.mode, 0, 0, 0);

  // Intentar leer el registro de estado (0x0F) para comprobar presencia física
  uint8_t reg = DS3232_REG_STATUS;
  uint8_t status = 0;
  esp_err_t ret = i2c_master_write_read_device(I2C_NUM, DS3232_I2C_ADDR, &reg, 1,
                                              &status, 1, pdMS_TO_TICKS(50));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "RTC DS3232 no detectado en bus I2C (addr 0x%02X): %s",
             DS3232_I2C_ADDR, esp_err_to_name(ret));
    return ret;
  }

  s_rtc_available = true;
  if (status & DS3232_STATUS_OSF) {
    ESP_LOGW(TAG, "RTC DS3232 detectado, pero con flag OSF activo (oscilador detenido o primer encendido)");
  } else {
    ESP_LOGI(TAG, "RTC DS3232 detectado y operativo en bus I2C (addr 0x%02X)", DS3232_I2C_ADDR);
  }

  return ESP_OK;
}

bool rtc_ds3232_is_available(void) {
  return s_rtc_available;
}

esp_err_t rtc_ds3232_get_time(rtc_time_t *time) {
  if (time == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (!s_rtc_available) {
    time->valid = false;
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t reg = DS3232_REG_SECONDS;
  uint8_t data[7] = {0};

  esp_err_t ret = i2c_master_write_read_device(I2C_NUM, DS3232_I2C_ADDR, &reg, 1,
                                              data, 7, pdMS_TO_TICKS(50));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error de comunicación I2C al leer RTC: %s", esp_err_to_name(ret));
    time->valid = false;
    return ret;
  }

  time->second = bcd_to_dec(data[0] & 0x7F);
  time->minute = bcd_to_dec(data[1] & 0x7F);

  // Formato de horas (registro 0x02)
  if (data[2] & 0x40) {
    // Modo 12 horas: bit 5 indica PM
    uint8_t h = bcd_to_dec(data[2] & 0x1F);
    if (data[2] & 0x20) {
      h += 12;
    }
    time->hour = h;
  } else {
    // Modo 24 horas: bits 5..0 contienen las horas (00-23)
    time->hour = bcd_to_dec(data[2] & 0x3F);
  }

  time->day   = bcd_to_dec(data[4] & 0x3F);
  time->month = bcd_to_dec(data[5] & 0x1F);
  time->year  = 2000 + bcd_to_dec(data[6]);
  time->valid = true;

  return ESP_OK;
}

esp_err_t rtc_ds3232_set_time(uint8_t day, uint8_t month, uint16_t year,
                              uint8_t hour, uint8_t minute, uint8_t second) {
  if (!s_rtc_available) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t buf[8];
  buf[0] = DS3232_REG_SECONDS;
  buf[1] = dec_to_bcd(second);
  buf[2] = dec_to_bcd(minute);
  buf[3] = dec_to_bcd(hour) & 0x3F; // Modo 24h (bit 6 en 0)
  buf[4] = 1;                       // Día de la semana (1-7, valor dummy)
  buf[5] = dec_to_bcd(day);
  buf[6] = dec_to_bcd(month);
  buf[7] = dec_to_bcd((uint8_t)(year % 100));

  esp_err_t ret = i2c_master_write_to_device(I2C_NUM, DS3232_I2C_ADDR, buf,
                                            sizeof(buf), pdMS_TO_TICKS(50));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al escribir fecha/hora en RTC: %s", esp_err_to_name(ret));
    return ret;
  }

  // Limpiar el flag OSF (Oscillator Stop Flag) en el registro de estado
  uint8_t status_clear[2] = {DS3232_REG_STATUS, 0x00};
  i2c_master_write_to_device(I2C_NUM, DS3232_I2C_ADDR, status_clear,
                             sizeof(status_clear), pdMS_TO_TICKS(50));

  ESP_LOGI(TAG, "RTC DS3232 calibrado por satélite: %02u:%02u:%02u %02u/%02u/%04u UTC",
           hour, minute, second, day, month, year);

  return ESP_OK;
}
