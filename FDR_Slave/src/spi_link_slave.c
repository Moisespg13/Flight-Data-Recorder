
#include "spi_link_slave.h"
#include "crc_utils.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/spi.h"
#include "pin_config.h"
#include "protocol.h"
#include "spi_slave_tx.pio.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t tx_buffer[sizeof(fdr_header_t) + FDR_MAX_PAYLOAD + 4];
static uint16_t s_seq_fast = 0;
static uint16_t s_seq_slow = 0;

static PIO s_pio = pio0;
static uint s_sm = 0;
static int s_dma_chan = -1;

void spi_link_slave_init(void) {
  // Configurar DRDY como salida (LOW por defecto)
  gpio_init(PIN_DRDY);
  gpio_set_dir(PIN_DRDY, GPIO_OUT);
  gpio_put(PIN_DRDY, 0);

  // Cargar programa PIO
  uint offset = pio_add_program(s_pio, &spi_slave_tx_program);
  s_sm = pio_claim_unused_sm(s_pio, true);

  // Inicializar PIO: MISO=GP1, SCK=GP2
  spi_slave_tx_program_init(s_pio, s_sm, offset, PIN_LINK_TX, PIN_LINK_SCK);

  // Claim DMA channel para alimentar el PIO TX FIFO
  s_dma_chan = dma_claim_unused_channel(true);

  printf("[SPI_LINK] PIO SPI Slave init (SM=%u DMA=%d MISO=GP%d SCK=GP%d DRDY=GP%d)\n",
         s_sm, s_dma_chan, PIN_LINK_TX, PIN_LINK_SCK, PIN_DRDY);
}

static void send_packet(uint8_t type, uint16_t *seq, uint64_t ts_ns,
                        const void *payload, uint16_t pay_len) {
  fdr_header_t *header = (fdr_header_t *)tx_buffer;
  header->magic = FDR_PROTO_MAGIC;
  header->type = type;
  header->sequence = (*seq)++;
  header->timestamp_ns = ts_ns;
  header->length = pay_len;
  memcpy(tx_buffer + sizeof(fdr_header_t), payload, pay_len);
  size_t data_len = sizeof(fdr_header_t) + pay_len;
  uint32_t crc = fdr_calc_crc32(tx_buffer, data_len);
  memcpy(tx_buffer + data_len, &crc, 4);
  size_t total_len = data_len + 4;

  // Señalizar al Maestro: "tengo datos listos"
  gpio_put(PIN_DRDY, 1);

  // Enviar byte a byte al PIO TX FIFO
  // pio_sm_put_blocking se bloquea si el FIFO está lleno,
  // esperando a que el Maestro clockee y drene datos.
  for (size_t i = 0; i < total_len; i++) {
    pio_sm_put_blocking(s_pio, s_sm, (uint32_t)tx_buffer[i] << 24);
  }

  // Paquete completo en el PIO → bajar DRDY
  gpio_put(PIN_DRDY, 0);

  static uint32_t pkt_cnt = 0;
  if (++pkt_cnt % 100 == 0)
    printf("[SPI_LINK] TX #%lu t=0x%02X\n", (unsigned long)pkt_cnt, type);
}

void spi_link_slave_send_fast(const fast_data_payload_t *payload) {
  if (!payload) return;
  send_packet(PKT_TYPE_FAST_DATA, &s_seq_fast, payload->timestamp_ns,
              payload, sizeof(fast_data_payload_t));
}

void spi_link_slave_send_slow(const slow_data_payload_t *payload) {
  if (!payload) return;
  send_packet(PKT_TYPE_SLOW_DATA, &s_seq_slow, payload->timestamp_ns,
              payload, sizeof(slow_data_payload_t));
}
