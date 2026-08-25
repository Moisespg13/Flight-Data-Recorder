
/**
 * @file imu_xsens.c
 * @brief Driver para el sensor IMU XSENS MTi-28A53G35.
 *
 * Implementa el parser del protocolo MT Communication Protocol
 * sobre UART. El sensor envía tramas binarias con la estructura:
 *
 *   Byte 0:   Preamble  (0xFA)
 *   Byte 1:   Bus ID    (0xFF)
 *   Byte 2:   Message ID
 *   Byte 3:   Length     (N bytes de datos, 0-254)
 *   Byte 4..N+3: Data
 *   Byte N+4: Checksum  (suma de bytes 1..N+3 + checksum = 0x00)
 *
 * El mensaje MTData (ID 0x32) contiene los datos de navegación
 * en formato float big-endian (IEEE 754).
 *
 * Orden de los 12 floats en MTData (modo Output = Calibrated + Orientation):
 *   [0..2]  Aceleración X, Y, Z       (m/s²)
 *   [3..5]  Velocidad angular X, Y, Z (rad/s)
 *   [6..8]  Campo magnético X, Y, Z   (unidades normalizadas)
 *   [9..11] Roll, Pitch, Yaw          (grados)
 */

#include "imu_xsens.h"
#include "pin_config.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "IMU_XSENS";

// ============================================================================
// Constantes del protocolo MT
// ============================================================================
#define MT_PREAMBLE     0xFA
#define MT_BUS_ID       0xFF
#define MT_MSG_MTDATA   0x32  // Mensaje con datos de navegación
#define MT_MSG_MTDATA2  0x36  // Mensaje con datos de navegación (formato 2)

// Tamaño esperado del payload MTData (12 floats * 4 bytes)
#define MT_DATA_LEN     48

// Tamaño máximo de una trama MT (preamble + busid + msgid + len + data + chk)
#define MT_MAX_FRAME    256

// Configuración UART
#define IMU_BAUD_RATE   115200
#define IMU_BUF_SIZE    512

// ============================================================================
// Estado del parser (máquina de estados)
// ============================================================================
typedef enum {
  PARSE_IDLE,       // Esperando preamble (0xFA)
  PARSE_BUS_ID,     // Esperando bus ID (0xFF)
  PARSE_MSG_ID,     // Esperando message ID
  PARSE_LENGTH,     // Esperando longitud del payload
  PARSE_DATA,       // Recibiendo datos
  PARSE_CHECKSUM    // Verificando checksum
} parse_state_t;

static parse_state_t s_state = PARSE_IDLE;
static uint8_t s_msg_id = 0;
static uint8_t s_data_len = 0;
static uint8_t s_data_buf[MT_MAX_FRAME];
static uint8_t s_data_pos = 0;
static uint8_t s_checksum = 0;

// Último dato parseado
static imu_payload_t s_imu_data = {0};
static bool s_data_valid = false;
static bool s_new_data = false;  // true solo cuando hay una trama nueva sin leer

// ============================================================================
// Funciones de conversión
// ============================================================================

/**
 * @brief Convierte 4 bytes big-endian a float.
 *
 * El XSENS envía floats en formato IEEE 754 big-endian (MSB primero).
 * El ESP32 es little-endian, así que invertimos los bytes.
 */
static float bytes_to_float_be(const uint8_t *buf) {
  uint32_t val = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                 ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
  float result;
  memcpy(&result, &val, sizeof(float));
  return result;
}

/**
 * @brief Procesa un mensaje MTData completo.
 *
 * Extrae los 12 floats del payload y los guarda en s_imu_data.
 */
static void process_mtdata(const uint8_t *data, uint8_t len) {
  if (len < MT_DATA_LEN) {
    ESP_LOGW(TAG, "MTData incompleto: %d bytes (esperados %d)", len,
             MT_DATA_LEN);
    return;
  }

  s_imu_data.timestamp_ns = (uint64_t)esp_timer_get_time() * 1000;

  // Aceleración (floats 0, 1, 2)
  s_imu_data.acc_x = bytes_to_float_be(&data[0]);
  s_imu_data.acc_y = bytes_to_float_be(&data[4]);
  s_imu_data.acc_z = bytes_to_float_be(&data[8]);

  // Giróscopo (floats 3, 4, 5)
  s_imu_data.gyr_x = bytes_to_float_be(&data[12]);
  s_imu_data.gyr_y = bytes_to_float_be(&data[16]);
  s_imu_data.gyr_z = bytes_to_float_be(&data[20]);

  // Magnetómetro (floats 6, 7, 8)
  s_imu_data.mag_x = bytes_to_float_be(&data[24]);
  s_imu_data.mag_y = bytes_to_float_be(&data[28]);
  s_imu_data.mag_z = bytes_to_float_be(&data[32]);

  // Ángulos de Euler (floats 9, 10, 11)
  s_imu_data.roll = bytes_to_float_be(&data[36]);
  s_imu_data.pitch = bytes_to_float_be(&data[40]);
  s_imu_data.yaw = bytes_to_float_be(&data[44]);

  s_data_valid = true;
  s_new_data = true;  // Marcar que hay datos frescos por leer

  ESP_LOGD(TAG, "IMU: acc=[%.2f, %.2f, %.2f] gyr=[%.2f, %.2f, %.2f]",
           s_imu_data.acc_x, s_imu_data.acc_y, s_imu_data.acc_z,
           s_imu_data.gyr_x, s_imu_data.gyr_y, s_imu_data.gyr_z);
}

/**
 * @brief Alimenta un byte a la máquina de estados del parser MT.
 *
 * La máquina busca la secuencia [0xFA][0xFF][MsgID][Len][Data...][Chk]
 * y cuando completa una trama válida, llama a process_mtdata().
 */
static void parse_byte(uint8_t byte) {
  switch (s_state) {

  case PARSE_IDLE:
    if (byte == MT_PREAMBLE) {
      s_state = PARSE_BUS_ID;
      s_checksum = 0; // Reset checksum (no incluye el preamble)
    }
    break;

  case PARSE_BUS_ID:
    if (byte == MT_BUS_ID) {
      s_checksum += byte;
      s_state = PARSE_MSG_ID;
    } else {
      s_state = PARSE_IDLE; // No es una trama MT válida
    }
    break;

  case PARSE_MSG_ID:
    s_msg_id = byte;
    s_checksum += byte;
    s_state = PARSE_LENGTH;
    break;

  case PARSE_LENGTH:
    s_data_len = byte;
    s_checksum += byte;
    s_data_pos = 0;

    if (s_data_len == 0) {
      s_state = PARSE_CHECKSUM; // Sin datos, directo al checksum
    } else {
      s_state = PARSE_DATA;
    }
    break;

  case PARSE_DATA:
    s_data_buf[s_data_pos++] = byte;
    s_checksum += byte;

    if (s_data_pos >= s_data_len) {
      s_state = PARSE_CHECKSUM;
    }
    break;

  case PARSE_CHECKSUM:
    s_checksum += byte;

    // Checksum válido si la suma de todo (BusID..Checksum) = 0x00
    if ((s_checksum & 0xFF) == 0x00) {
      // Trama válida: procesar según el tipo de mensaje
      if (s_msg_id == MT_MSG_MTDATA) {
        process_mtdata(s_data_buf, s_data_len);
      }
      // TODO: Soportar MT_MSG_MTDATA2 si el sensor usa firmware nuevo
    } else {
      ESP_LOGW(TAG, "Checksum inválido: 0x%02X (esperado 0x00)",
               s_checksum & 0xFF);
    }

    s_state = PARSE_IDLE;
    break;
  }
}

// ============================================================================
// API Pública
// ============================================================================

esp_err_t imu_xsens_init(void) {
  ESP_LOGI(TAG, "Inicializando IMU XSENS (UART%d, %d baud)...", IMU_UART_NUM,
           IMU_BAUD_RATE);

  uart_config_t uart_config = {
      .baud_rate = IMU_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t ret = uart_param_config(IMU_UART_NUM, &uart_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar UART: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = uart_set_pin(IMU_UART_NUM, PIN_IMU_TX, PIN_IMU_RX,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al asignar pines UART: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = uart_driver_install(IMU_UART_NUM, IMU_BUF_SIZE * 2, 0, 0, NULL, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al instalar driver UART: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "IMU XSENS inicializada en UART%d (RX=GPIO%d, TX=GPIO%d)",
           IMU_UART_NUM, PIN_IMU_RX, PIN_IMU_TX);
  return ESP_OK;
}

esp_err_t imu_xsens_process(void) {
  uint8_t byte;
  int count = 0;

  // Leer todos los bytes disponibles y alimentar al parser
  while (uart_read_bytes(IMU_UART_NUM, &byte, 1, 0) > 0) {
    parse_byte(byte);
    count++;
  }

  return (count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bool imu_xsens_get_data(imu_payload_t *payload) {
  if (payload == NULL || !s_data_valid || !s_new_data) {
    return false;
  }

  *payload = s_imu_data;
  s_new_data = false;  // Consumido: no devolver el mismo dato otra vez
  return true;
}
