/* RTR-OS - controlador de interrupções GIC-400 (GICv2) do BCM2711. */
#include <stddef.h>

#include "arch.h"
#include "board.h"

#define GICD_BASE           (BOARD_GIC_BASE + 0x1000UL)
#define GICD_CTLR           (GICD_BASE + 0x000UL)
#define GICD_ISENABLER      (GICD_BASE + 0x100UL)
#define GICD_IPRIORITYR     (GICD_BASE + 0x400UL)

#define GICC_BASE           (BOARD_GIC_BASE + 0x2000UL)
#define GICC_CTLR           (GICC_BASE + 0x000UL)
#define GICC_PMR            (GICC_BASE + 0x004UL)
#define GICC_IAR            (GICC_BASE + 0x00CUL)
#define GICC_EOIR           (GICC_BASE + 0x010UL)

#define IRQ_COUNT           1020U       /* de 1020 em diante não há interrupção real */
#define IRQ_ID_MASK         0x3FFU
#define IRQ_PRIORITY        0xA0U
#define PRIORITY_MASK_ALL   0xFFU

void gic_init(void)
{
    mmio_write32(GICD_CTLR, 1U);
    mmio_write32(GICC_PMR, PRIORITY_MASK_ALL);
    mmio_write32(GICC_CTLR, 1U);
}

void gic_enable_irq(uint32_t irq)
{
    uintptr_t priority_reg;
    uint32_t shift;
    uint32_t value;

    if (irq >= IRQ_COUNT) {
        return;
    }

    /* Um byte de prioridade por interrupção, quatro por registrador. */
    priority_reg = GICD_IPRIORITYR + (uintptr_t)(irq / 4U) * 4U;
    shift = (irq % 4U) * 8U;
    value = mmio_read32(priority_reg);
    value &= ~(0xFFU << shift);
    value |= IRQ_PRIORITY << shift;
    mmio_write32(priority_reg, value);

    /* Um bit de habilitação por interrupção, 32 por registrador. */
    mmio_write32(GICD_ISENABLER + (uintptr_t)(irq / 32U) * 4U, 1U << (irq % 32U));
}

uint32_t gic_acknowledge(uint32_t *token)
{
    uint32_t iar = mmio_read32(GICC_IAR);
    uint32_t irq = iar & IRQ_ID_MASK;

    if (token != NULL) {
        *token = iar;
    }
    return (irq < IRQ_COUNT) ? irq : GIC_IRQ_NONE;
}

void gic_end_of_irq(uint32_t token)
{
    mmio_write32(GICC_EOIR, token);
}
