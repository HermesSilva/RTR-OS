/* RTR-OS - drivers da placa (BCM2711 / Raspberry Pi 4). */
#ifndef RTR_DRIVERS_H
#define RTR_DRIVERS_H

#include <stdint.h>

#define PERIPHERAL_BASE 0xFE000000UL
#define GIC_BASE        0xFF840000UL

#define IRQ_TIMER       30u             /* PPI do timer físico de EL1 */
#define IRQ_SPURIOUS    1020u           /* de 1020 em diante não há interrupção real */

/* UART0 (PL011) nos pinos GPIO14/15, 115200 8N1 */
void uart_init(void);
void uart_putc(char c);

/* GIC-400 */
void gic_init(void);
void gic_enable_irq(uint32_t irq);
uint32_t gic_acknowledge(void);
void gic_end_of_irq(uint32_t iar);

/* Timer genérico do ARM, usado em modo one-shot */
void timer_init(void);
uint64_t timer_hz(void);
uint64_t timer_now(void);
void timer_set(uint64_t deadline);
uint64_t timer_us_to_ticks(uint64_t us);
uint64_t timer_ticks_to_us(uint64_t ticks);

#endif
