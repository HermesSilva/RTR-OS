/* RTR-OS - BCM2711 UART0 (PL011). */
#include "arch.h"
#include "board.h"

#define GPFSEL1         (BOARD_GPIO_BASE + 0x04UL)
#define GPIO_PUP_PDN0   (BOARD_GPIO_BASE + 0xE4UL)

#define UART_DR         (BOARD_UART0_BASE + 0x00UL)
#define UART_FR         (BOARD_UART0_BASE + 0x18UL)
#define UART_IBRD       (BOARD_UART0_BASE + 0x24UL)
#define UART_FBRD       (BOARD_UART0_BASE + 0x28UL)
#define UART_LCRH       (BOARD_UART0_BASE + 0x2CUL)
#define UART_CR         (BOARD_UART0_BASE + 0x30UL)
#define UART_ICR        (BOARD_UART0_BASE + 0x44UL)

#define FR_TXFF         (1U << 5U)
#define LCRH_FIFO_8N1   ((3U << 5U) | (1U << 4U))
#define CR_ENABLE_TX_RX ((1U << 9U) | (1U << 8U) | 1U)
#define ICR_ALL         0x7FFU

#define GPIO_ALT0       4U
#define GPIO14_FSEL     12U
#define GPIO15_FSEL     15U
#define GPIO14_PULL     28U
#define GPIO15_PULL     30U

/* Divisor for 115200 baud: 48e6 / (16 * 115200) = 26.04 -> 26 integer, 3/64 fractional. */
#define BAUD_INTEGER    26U
#define BAUD_FRACTION   3U

/*
 * One character takes about 87 us at 115200 baud. The limit below is far
 * more than that even on the fastest processor, and keeps a stuck
 * transmitter from holding the kernel.
 */
#define TX_WAIT_LIMIT   1000000U

void uart_init(void)
{
    uint32_t value;

    mmio_write32(UART_CR, 0U);

    /* GPIO14 and GPIO15 as ALT0 (TXD0/RXD0), no pull */
    value = mmio_read32(GPFSEL1);
    value &= ~((7U << GPIO14_FSEL) | (7U << GPIO15_FSEL));
    value |= (GPIO_ALT0 << GPIO14_FSEL) | (GPIO_ALT0 << GPIO15_FSEL);
    mmio_write32(GPFSEL1, value);

    value = mmio_read32(GPIO_PUP_PDN0);
    value &= ~((3U << GPIO14_PULL) | (3U << GPIO15_PULL));
    mmio_write32(GPIO_PUP_PDN0, value);

    mmio_write32(UART_ICR, ICR_ALL);
    mmio_write32(UART_IBRD, BAUD_INTEGER);
    mmio_write32(UART_FBRD, BAUD_FRACTION);
    mmio_write32(UART_LCRH, LCRH_FIFO_8N1);
    mmio_write32(UART_CR, CR_ENABLE_TX_RX);
}

bool uart_send(uint8_t byte)
{
    for (uint32_t attempt = 0U; attempt < TX_WAIT_LIMIT; attempt++) {
        if ((mmio_read32(UART_FR) & FR_TXFF) == 0U) {
            mmio_write32(UART_DR, byte);
            return true;
        }
    }
    return false;
}
