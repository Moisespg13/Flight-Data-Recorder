
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
#include "hardware/timer.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

static uint8_t tx_buffer[sizeof(fdr_header_t) + FDR_MAX_PAYLOAD + 4];
static uint16_t s_seq_fast = 0;
static uint16_t s_seq_slow = 0;

static PIO s_pio = pio0;
static uint s_sm = 0;
static uint s_offset = 0;
static int s_dma_chan = -1;

void spi_link_slave_init(void) {
  // Configurar DRDY como salida (LOW por defecto)
  gpio_init(PIN_DRDY);
  gpio_set_dir(PIN_DRDY, GPIO_OUT);
  gpio_put(PIN_DRDY, 0);

  // Cargar programa PIO
  s_offset = pio_add_program(s_pio, &spi_slave_tx_program);
  s_sm = pio_claim_unused_sm(s_pio, true);

  // Inicializar PIO: MISO=GP1, SCK=GP2
  spi_slave_tx_program_init(s_pio, s_sm, s_offset, PIN_LINK_TX, PIN_LINK_SCK);

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

  // === PASO 1: Reiniciar PIO completamente ===
  // Desactivar para evitar que procese ruido en SCK
  pio_sm_set_enabled(s_pio, s_sm, false);
  pio_sm_restart(s_pio, s_sm);
  pio_sm_clear_fifos(s_pio, s_sm);
  pio_sm_exec(s_pio, s_sm, pio_encode_jmp(s_offset));
  pio_sm_set_enabled(s_pio, s_sm, true);

  // === PASO 1.5: Preparar buffer de 32-bits para el PIO ===
  // Como el PIO está configurado con shift_right=false (MSB first), 
  // la instrucción `out pins, 1` toma el bit 31.
  // Evitamos escrituras sub-word al TXF expandiendo a 32-bits.
  static uint32_t tx_buffer_32[sizeof(tx_buffer)];
  for(size_t i = 0; i < total_len; i++) {
      tx_buffer_32[i] = ((uint32_t)tx_buffer[i]) << 24;
  }

  // === PASO 2: Configurar y arrancar DMA ===
  // El DMA transfiere en background sin importar la CPU.
  dma_channel_config c = dma_channel_get_default_config(s_dma_chan);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
  channel_config_set_read_increment(&c, true);
  channel_config_set_write_increment(&c, false);
  channel_config_set_dreq(&c, pio_get_dreq(s_pio, s_sm, true));

  dma_channel_configure(
      s_dma_chan,
      &c,
      &s_pio->txf[s_sm], // Destino: PIO TX FIFO
      tx_buffer_32,      // Origen: Buffer de datos 32-bit
      total_len,         // Cantidad de palabras (1 por byte)
      true               // Iniciar inmediatamente
  );

  // === PASO 3: Preparar interrupción de fin de transacción ===
  // Limpiamos notificaciones anteriores de CS (Index 2)
  ulTaskNotifyTakeIndexed(2, pdTRUE, 0); 
  // Habilitamos interrupción por hardware (el callback está en main.c)
  gpio_set_irq_enabled(PIN_LINK_CS, GPIO_IRQ_EDGE_RISE, true);

  // === PASO 4: Señalizar DRDY HIGH (datos listos) ===
  // Al subir DRDY, el Maestro arranca el reloj. 
  gpio_put(PIN_DRDY, 1);

  // === PASO 5: Dormir profundamente (0% CPU) hasta que el Maestro termine ===
  // La ISR de CS HIGH despertará a esta tarea. Timeout de 10ms por si el Maestro muere.
  if (ulTaskNotifyTakeIndexed(2, pdTRUE, pdMS_TO_TICKS(10)) == 0) {
      // Timeout: el maestro nunca terminó la transacción. Abortar DMA para no trabar el hardware.
      dma_channel_abort(s_dma_chan);
  }

  // === PASO 6: Limpieza ===
  // Deshabilitar IRQ para evitar triggers espurios mientras armamos el siguiente paquete
  gpio_set_irq_enabled(PIN_LINK_CS, GPIO_IRQ_EDGE_RISE, false);

  // === PASO 7: Bajar DRDY (handshake completo) ===
  gpio_put(PIN_DRDY, 0);

  static uint32_t pkt_cnt = 0;
  if (++pkt_cnt % 3000 == 0)
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
