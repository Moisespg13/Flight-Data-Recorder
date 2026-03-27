
/**
 * @file spi_link_slave.c
 * @brief Enlace SPI Slave inter-placa (RP2040 -> ESP32-S3).
 *
 * Implementa la inicialización de SPI1 como Slave y la transmisión
 * de paquetes al Maestro. Arma paquetes con Header + Payload + CRC32.
 *
 * NOTA: El Pico SDK no tiene un driver SPI Slave nativo tan robusto
 * como el ESP-IDF. Se usa la configuración de hardware SPI en modo
 * Slave y transferencia por bloque.
 *
 * TODO: Evaluar el uso de PIO para un Slave SPI más robusto y con
 * buffer FIFO más grande si el hardware SPI no alcanza.
 */

#include "spi_link_slave.h"
#include "crc_utils.h"
#include "pin_config.h"
#include "protocol.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <string.h>
#include <stdio.h>

// Buffer de transmisión
static uint8_t tx_buffer[sizeof(fdr_header_t) + FDR_MAX_PAYLOAD + 4];

// Contador de secuencia
static uint16_t s_sequence = 0;

void spi_link_slave_init(void) {
  // Inicializar SPI1 como Slave
  spi_init(PIN_LINK_SPI_INST, 10 * 1000 * 1000);
  spi_set_slave(PIN_LINK_SPI_INST, true);

  // Asignar funciones SPI a los pines del bus de enlace
  gpio_set_function(PIN_LINK_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_LINK_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_LINK_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_LINK_CS, GPIO_FUNC_SPI);

  printf("[SPI_LINK] SPI Slave inicializado en SPI1\n");
  printf("[SPI_LINK] SCK=%d, MOSI=%d, MISO=%d, CS=%d\n",
         PIN_LINK_SCK, PIN_LINK_MOSI, PIN_LINK_MISO, PIN_LINK_CS);
}

void spi_link_slave_send(const fast_data_payload_t *payload) {
  if (payload == NULL) {
    return;
  }

  // 1. Armar cabecera
  fdr_header_t *header = (fdr_header_t *)tx_buffer;
  header->magic = FDR_PROTO_MAGIC;
  header->type = PKT_TYPE_FAST_DATA;
  header->sequence = s_sequence++;
  header->timestamp_ns = payload->timestamp_ns;
  header->length = sizeof(fast_data_payload_t);

  // 2. Copiar payload después del header
  memcpy(tx_buffer + sizeof(fdr_header_t), payload,
         sizeof(fast_data_payload_t));

  // 3. Calcular CRC32
  size_t data_len = sizeof(fdr_header_t) + sizeof(fast_data_payload_t);
  uint32_t crc = fdr_calc_crc32(tx_buffer, data_len);

  // 4. Agregar CRC al final
  memcpy(tx_buffer + data_len, &crc, 4);
  size_t total_len = data_len + 4;

  // 5. Escribir en el buffer SPI Slave
  //    La transferencia real sucede cuando el Maestro genera CLK + CS
  spi_write_blocking(PIN_LINK_SPI_INST, tx_buffer, total_len);
}
