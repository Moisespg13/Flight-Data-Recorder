# spi_link

Enlace SPI2 hacia el RP2040. El maestro es SPI master a 10 MHz.

| Señal | GPIO |
|-------|:----:|
| MISO | 13 |
| CLK | 12 |
| CS | 10 |
| DRDY | 11 |

`tsk_spi_link` corre en el núcleo 1 con prioridad 12. Espera el flanco de DRDY y clockea un paquete. Valida magic `0xFD` y CRC32.

Un paquete rápido mide 178 bytes: cabecera de 14, cuatro muestras de 40 y CRC32 de 4. Cada muestra trae su timestamp; el de la cabecera es el de la primera. El maestro las separa y encola cada `fast_data_payload_t`. Un paquete lento es una sola muestra (58 bytes en total).

`spi_manager.c` inicializa el bus. El buffer de recepción está dimensionado al paquete rápido (`MAX_PKT_LEN` = 178).
