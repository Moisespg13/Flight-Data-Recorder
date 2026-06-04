
/**
 * @file cmd_port.c
 * @brief Puerto de Comandos RS-232 para configuración del MAD.
 *
 * Implementa el protocolo de comunicación pregunta-respuesta entre
 * la notebook del técnico (Master) y el MAD (Slave), tal como lo
 * define la especificación del CEV:
 *
 *   Notebook envía: [0xAA55] [Comando] [Dato]  (6 bytes)
 *   MAD responde:   [0xAA55] [Comando] [Valor] (6 bytes)
 *
 * Los formatos de muestreo se almacenan en la NVS (Non-Volatile Storage)
 * del ESP32, que persiste entre reinicios y cortes de energía.
 *
 * Nota sobre el byte order:
 *   El protocolo usa little-endian (el estándar del ESP32).
 *   Si la notebook envía big-endian, ajustar con ntohs/htons.
 */

#include "cmd_port.h"
#include "pin_config.h"
#include "sd_storage.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "CMD_PORT";

// Configuración UART
#define CMD_BAUD_RATE 9600
#define CMD_BUF_SIZE  256

// Clave NVS para almacenar el formato de muestreo
#define NVS_NAMESPACE    "fdr_config"
#define NVS_KEY_FORMAT   "sample_fmt"

// Tabla de valores actuales de canales (actualizada por las tareas de adquisición)
// Canales 0..15:  analógicos rápidos (ANA 1..16)
// Canales 16..31: analógicos lentos (ANA 17..32)
// Canales 32..47: discretas (bit a bit)
#define MAX_CHANNELS 48
static int16_t s_channel_values[MAX_CHANNELS] = {0};

// Buffer de recepción para acumular bytes hasta tener un mensaje completo
static uint8_t s_rx_buf[sizeof(cmd_message_t)];
static int s_rx_pos = 0;

// ============================================================================
// Funciones internas
// ============================================================================

/**
 * @brief Envía un mensaje de respuesta por UART.
 */
static void send_response(uint16_t command, uint16_t data) {
  cmd_message_t resp;
  resp.header = CMD_HEADER;
  resp.command = command;
  resp.data = data;

  uart_write_bytes(CMD_UART_NUM, (const char *)&resp, sizeof(cmd_message_t));

  ESP_LOGD(TAG, "TX: cmd=0x%04X data=0x%04X", command, data);
}

/**
 * @brief Ejecuta CMD 1: Lectura de parámetro en tiempo real.
 *
 * El campo 'data' del mensaje contiene el número de canal (0..47).
 * El MAD responde con el valor actual de ese canal.
 */
static void handle_read_param(uint16_t channel) {
  if (channel >= MAX_CHANNELS) {
    ESP_LOGW(TAG, "CMD1: Canal inválido: %d", channel);
    send_response(CMD_READ_PARAM, CMD_ACK_ERROR);
    return;
  }

  int16_t value = s_channel_values[channel];
  send_response(CMD_READ_PARAM, (uint16_t)value);

  ESP_LOGD(TAG, "CMD1: Canal %d = %d", channel, value);
}

/**
 * @brief Ejecuta CMD 2: Escritura de formato de muestreo en NVS.
 *
 * Guarda el número de formato (1, 2 o 3) en la NVS del ESP32.
 * Este valor persiste entre reinicios y cortes de energía.
 */
static void handle_write_format(uint16_t format) {
  if (format < 1 || format > 3) {
    ESP_LOGW(TAG, "CMD2: Formato inválido: %d (debe ser 1, 2 o 3)", format);
    send_response(CMD_WRITE_FORMAT, CMD_ACK_ERROR);
    return;
  }

  // Abrir NVS y guardar el formato
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "CMD2: Error al abrir NVS: %s", esp_err_to_name(err));
    send_response(CMD_WRITE_FORMAT, CMD_ACK_ERROR);
    return;
  }

  err = nvs_set_u16(nvs, NVS_KEY_FORMAT, format);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "CMD2: Error al escribir NVS: %s", esp_err_to_name(err));
    nvs_close(nvs);
    send_response(CMD_WRITE_FORMAT, CMD_ACK_ERROR);
    return;
  }

  err = nvs_commit(nvs);
  nvs_close(nvs);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "CMD2: Error al hacer commit NVS: %s", esp_err_to_name(err));
    send_response(CMD_WRITE_FORMAT, CMD_ACK_ERROR);
    return;
  }

  ESP_LOGI(TAG, "CMD2: Formato de muestreo guardado: %d", format);
  send_response(CMD_WRITE_FORMAT, CMD_ACK_OK);
}

/**
 * @brief Ejecuta CMD 3: Lectura del formato de muestreo almacenado.
 *
 * Lee el número de formato desde la NVS. Si no hay ninguno guardado,
 * devuelve 1 (Formato 1 = por defecto, máxima resolución).
 */
static void handle_read_format(void) {
  nvs_handle_t nvs;
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "CMD3: NVS no disponible, devolviendo formato por defecto");
    send_response(CMD_READ_FORMAT, 1); // Default: Formato 1
    return;
  }

  uint16_t format = 1; // Default
  err = nvs_get_u16(nvs, NVS_KEY_FORMAT, &format);
  nvs_close(nvs);

  if (err == ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGI(TAG, "CMD3: Sin formato guardado, devolviendo default (1)");
    format = 1;
  } else if (err != ESP_OK) {
    ESP_LOGE(TAG, "CMD3: Error al leer NVS: %s", esp_err_to_name(err));
    send_response(CMD_READ_FORMAT, CMD_ACK_ERROR);
    return;
  }

  ESP_LOGI(TAG, "CMD3: Formato de muestreo actual: %d", format);
  send_response(CMD_READ_FORMAT, format);
}

/**
 * @brief Ejecuta CMD 4: Formatear MicroSD.
 *
 * Requiere que el campo 'data' contenga la clave de seguridad
 * CMD_FORMAT_KEY (0x55AA) para prevenir formateos accidentales.
 */
static void handle_format_sd(uint16_t key) {
  if (key != CMD_FORMAT_KEY) {
    ESP_LOGW(TAG, "CMD4: Clave de formateo incorrecta: 0x%04X (esperado 0x%04X)",
             key, CMD_FORMAT_KEY);
    send_response(CMD_FORMAT_SD, CMD_ACK_ERROR);
    return;
  }

  ESP_LOGW(TAG, "CMD4: Comando de formateo recibido con clave correcta");

  esp_err_t ret = sd_storage_format();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "CMD4: Error al formatear MicroSD");
    send_response(CMD_FORMAT_SD, CMD_ACK_ERROR);
    return;
  }

  ESP_LOGI(TAG, "CMD4: MicroSD formateada exitosamente");
  send_response(CMD_FORMAT_SD, CMD_ACK_OK);

  // Reiniciar la placa para arrancar con la memoria limpia y estado consistente
  ESP_LOGW(TAG, "CMD4: Reiniciando el sistema en 2 segundos...");
  vTaskDelay(pdMS_TO_TICKS(2000));
  esp_restart();
}

/**
 * @brief Procesa un mensaje completo recibido.
 */
static void process_message(const cmd_message_t *msg) {
  // Verificar encabezado
  if (msg->header != CMD_HEADER) {
    ESP_LOGW(TAG, "Header inválido: 0x%04X (esperado 0x%04X)", msg->header,
             CMD_HEADER);
    return;
  }

  ESP_LOGD(TAG, "RX: cmd=0x%04X data=0x%04X", msg->command, msg->data);

  switch (msg->command) {
  case CMD_READ_PARAM:
    handle_read_param(msg->data);
    break;
  case CMD_WRITE_FORMAT:
    handle_write_format(msg->data);
    break;
  case CMD_READ_FORMAT:
    handle_read_format();
    break;
  case CMD_FORMAT_SD:
    handle_format_sd(msg->data);
    break;
  default:
    ESP_LOGW(TAG, "Comando desconocido: 0x%04X", msg->command);
    send_response(msg->command, CMD_ACK_ERROR);
    break;
  }
}

// ============================================================================
// API Pública
// ============================================================================

esp_err_t cmd_port_init(void) {
  // Para TB4 compartimos el UART0 (Consola). Usamos 115200 baudios en lugar de 9600.
  int baud_rate = (CMD_UART_NUM == 0) ? 115200 : CMD_BAUD_RATE;

  ESP_LOGI(TAG, "Inicializando Puerto de Comandos (UART%d, %d baud)...",
           CMD_UART_NUM, baud_rate);

  uart_config_t uart_config = {
      .baud_rate = baud_rate,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  esp_err_t ret = uart_param_config(CMD_UART_NUM, &uart_config);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Error al configurar UART: %s", esp_err_to_name(ret));
    // No abortamos, seguimos intentando
  }

  // Set pin
  if (PIN_CMD_TX != UART_PIN_NO_CHANGE || PIN_CMD_RX != UART_PIN_NO_CHANGE) {
    ret = uart_set_pin(CMD_UART_NUM, PIN_CMD_TX, PIN_CMD_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Error al asignar pines UART: %s", esp_err_to_name(ret));
    }
  }

  ret = uart_driver_install(CMD_UART_NUM, CMD_BUF_SIZE * 2, CMD_BUF_SIZE, 0,
                            NULL, 0);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    // ESP_ERR_INVALID_STATE significa que ya estaba instalado (ej. consola)
    ESP_LOGE(TAG, "Error al instalar driver UART: %s", esp_err_to_name(ret));
    return ret;
  }

  s_rx_pos = 0;

  ESP_LOGI(TAG, "Puerto de Comandos listo en UART%d", CMD_UART_NUM);
  return ESP_OK;
}

esp_err_t cmd_port_process(void) {
  uint8_t byte;
  bool processed = false;

  while (uart_read_bytes(CMD_UART_NUM, &byte, 1, 0) > 0) {
    s_rx_buf[s_rx_pos++] = byte;

    // ¿Tenemos un mensaje completo? (6 bytes)
    if (s_rx_pos >= (int)sizeof(cmd_message_t)) {
      cmd_message_t msg;
      memcpy(&msg, s_rx_buf, sizeof(cmd_message_t));

      // Verificar si el header es correcto
      if (msg.header == CMD_HEADER) {
        process_message(&msg);
        processed = true;
        s_rx_pos = 0;
      } else {
        // Header incorrecto: descartar el primer byte y desplazar el buffer
        // (buscar sincronización byte a byte)
        memmove(s_rx_buf, s_rx_buf + 1, s_rx_pos - 1);
        s_rx_pos--;
      }
    }
  }

  return processed ? ESP_OK : ESP_ERR_NOT_FOUND;
}

void cmd_port_update_channel(uint8_t channel, int16_t value) {
  if (channel < MAX_CHANNELS) {
    s_channel_values[channel] = value;
  }
}
