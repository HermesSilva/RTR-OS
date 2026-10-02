/* RTR-OS - mapa de endereços e dispositivos da placa (BCM2711 / Raspberry Pi 4). */
#ifndef RTR_BOARD_H
#define RTR_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* Janela dos periféricos: de 0xFC000000 até o fim dos primeiros 4 GB. */
#define BOARD_DEVICE_START      0xFC000000UL
#define BOARD_DEVICE_END        0x100000000UL

#define BOARD_PERIPHERAL_BASE   0xFE000000UL
#define BOARD_GPIO_BASE         (BOARD_PERIPHERAL_BASE + 0x200000UL)
#define BOARD_UART0_BASE        (BOARD_PERIPHERAL_BASE + 0x201000UL)
#define BOARD_GIC_BASE          0xFF840000UL

/* Clock da UART0, fixado em sdcard/config.txt (init_uart_clock). */
#define BOARD_UART0_CLOCK_HZ    48000000UL

#define BOARD_IRQ_TIMER         30U     /* timer físico de EL1, privado de cada núcleo */

/* UART0 (PL011) nos pinos GPIO14/15, 115200 8N1: console de depuração do kernel. */
void uart_init(void);

/* Devolve false se o transmissor não liberou espaço dentro do tempo limite. */
bool uart_send(uint8_t byte) __attribute__((warn_unused_result));

/* Controlador de interrupções GIC-400. */
#define GIC_IRQ_NONE            1023U

void gic_init(void);
void gic_enable_irq(uint32_t irq);

/* Devolve o número da interrupção pendente, ou GIC_IRQ_NONE; `token` vai para gic_end_of_irq. */
uint32_t gic_acknowledge(uint32_t *token) __attribute__((warn_unused_result));
void gic_end_of_irq(uint32_t token);

#endif
