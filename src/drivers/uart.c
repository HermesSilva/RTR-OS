/* RTR-OS - console serial pela UART0 (PL011) do BCM2711. */
#include "arch.h"
#include "drivers.h"

#define GPIO_BASE       (PERIPHERAL_BASE + 0x200000)
#define GPFSEL1         (GPIO_BASE + 0x04)
#define GPIO_PUP_PDN0   (GPIO_BASE + 0xE4)

#define UART0_BASE      (PERIPHERAL_BASE + 0x201000)
#define UART0_DR        (UART0_BASE + 0x00)
#define UART0_FR        (UART0_BASE + 0x18)
#define UART0_IBRD      (UART0_BASE + 0x24)
#define UART0_FBRD      (UART0_BASE + 0x28)
#define UART0_LCRH      (UART0_BASE + 0x2C)
#define UART0_CR        (UART0_BASE + 0x30)
#define UART0_ICR       (UART0_BASE + 0x44)

#define FR_TXFF         (1u << 5)
#define FR_BUSY         (1u << 3)

void uart_init(void)
{
    uint32_t v;

    mmio_write(UART0_CR, 0);

    /* GPIO14 e GPIO15 em ALT0 (TXD0/RXD0), sem pull */
    v = mmio_read(GPFSEL1);
    v &= ~((7u << 12) | (7u << 15));
    v |= (4u << 12) | (4u << 15);
    mmio_write(GPFSEL1, v);

    v = mmio_read(GPIO_PUP_PDN0);
    v &= ~((3u << 28) | (3u << 30));
    mmio_write(GPIO_PUP_PDN0, v);

    /* 115200 baud com o clock de 48 MHz fixado em config.txt: 48e6 / (16 * 115200) = 26,04 */
    mmio_write(UART0_ICR, 0x7FF);
    mmio_write(UART0_IBRD, 26);
    mmio_write(UART0_FBRD, 3);
    mmio_write(UART0_LCRH, (3u << 5) | (1u << 4));  /* 8 bits, FIFO ligada */
    mmio_write(UART0_CR, (1u << 9) | (1u << 8) | 1u);
}

void uart_putc(char c)
{
    if (c == '\n')
        uart_putc('\r');

    while (mmio_read(UART0_FR) & FR_TXFF)
        ;
    mmio_write(UART0_DR, (uint8_t)c);
}
