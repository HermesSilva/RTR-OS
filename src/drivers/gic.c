/* RTR-OS - controlador de interrupções GIC-400 (GICv2) do BCM2711. */
#include "arch.h"
#include "drivers.h"

#define GICD_BASE           (GIC_BASE + 0x1000)
#define GICD_CTLR           (GICD_BASE + 0x000)
#define GICD_ISENABLER(n)   (GICD_BASE + 0x100 + 4 * (n))
#define GICD_IPRIORITYR(n)  (GICD_BASE + 0x400 + 4 * (n))

#define GICC_BASE           (GIC_BASE + 0x2000)
#define GICC_CTLR           (GICC_BASE + 0x000)
#define GICC_PMR            (GICC_BASE + 0x004)
#define GICC_IAR            (GICC_BASE + 0x00C)
#define GICC_EOIR           (GICC_BASE + 0x010)

#define IRQ_PRIORITY        0xA0u

void gic_init(void)
{
    mmio_write(GICD_CTLR, 1);
    mmio_write(GICC_PMR, 0xFF);
    mmio_write(GICC_CTLR, 1);
}

void gic_enable_irq(uint32_t irq)
{
    uint32_t shift = (irq % 4) * 8;
    uint32_t prio = mmio_read(GICD_IPRIORITYR(irq / 4));

    prio &= ~(0xFFu << shift);
    prio |= IRQ_PRIORITY << shift;
    mmio_write(GICD_IPRIORITYR(irq / 4), prio);

    mmio_write(GICD_ISENABLER(irq / 32), 1u << (irq % 32));
}

uint32_t gic_acknowledge(void)
{
    return mmio_read(GICC_IAR);
}

void gic_end_of_irq(uint32_t iar)
{
    mmio_write(GICC_EOIR, iar);
}
