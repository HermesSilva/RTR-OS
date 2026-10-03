/* RTR-OS - board address map and devices (BCM2711 / Raspberry Pi 4). */
#ifndef RTR_BOARD_H
#define RTR_BOARD_H

#include <stdbool.h>
#include <stdint.h>

/* Peripheral window: from 0xFC000000 to the end of the first 4 GB. */
#define BOARD_DEVICE_START      0xFC000000UL
#define BOARD_DEVICE_END        0x100000000UL

#define BOARD_PERIPHERAL_BASE   0xFE000000UL
#define BOARD_GPIO_BASE         (BOARD_PERIPHERAL_BASE + 0x200000UL)
#define BOARD_UART0_BASE        (BOARD_PERIPHERAL_BASE + 0x201000UL)
#define BOARD_GIC_BASE          0xFF840000UL

/* Devices not used by the kernel, only assigned to processes. */
#define BOARD_GENET_BASE        0xFD580000UL    /* Ethernet controller (GENET) */
#define BOARD_GENET_SIZE        0x10000UL
#define BOARD_EMMC2_BASE        (BOARD_PERIPHERAL_BASE + 0x340000UL)  /* SD card host (EMMC2) */
#define BOARD_EMMC2_SIZE        0x1000UL
#define BOARD_GPIO_SIZE         0x1000UL        /* the GPIO block, assignable as a whole */

/* Power management: used by the kernel only to reset the board. */
#define BOARD_PM_BASE           (BOARD_PERIPHERAL_BASE + 0x100000UL)

/* Resets the board through the watchdog. Does not return. */
void board_reboot(void) __attribute__((noreturn));

/* UART0 clock, fixed in sdcard/config.txt (init_uart_clock). */
#define BOARD_UART0_CLOCK_HZ    48000000UL

#define BOARD_IRQ_TIMER         30U     /* EL1 physical timer, private to each core */

/* UART0 (PL011) on GPIO14/15, 115200 8N1: the kernel debug console. */
void uart_init(void);

/* Returns false if the transmitter did not free space within the time limit. */
bool uart_send(uint8_t byte) __attribute__((warn_unused_result));

/* GIC-400 interrupt controller. */
#define GIC_IRQ_NONE            1023U

void gic_init(void);
void gic_enable_irq(uint32_t irq);

/* Returns the pending interrupt number, or GIC_IRQ_NONE; `token` goes to gic_end_of_irq. */
uint32_t gic_acknowledge(uint32_t *token) __attribute__((warn_unused_result));
void gic_end_of_irq(uint32_t token);

#endif
