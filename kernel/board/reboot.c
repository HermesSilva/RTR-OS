/* RTR-OS - board reset through the BCM2711 power management watchdog. */
#include "arch.h"
#include "board.h"

#define PM_RSTC             (BOARD_PM_BASE + 0x1CUL)
#define PM_WDOG             (BOARD_PM_BASE + 0x24UL)
#define PM_PASSWORD         0x5A000000U
#define PM_RSTC_WRCFG_MASK  0x00000030U
#define PM_RSTC_FULL_RESET  0x00000020U
#define PM_WDOG_TICKS       10U         /* about 150 us at 65536 ticks per second */

void board_reboot(void)
{
    uint32_t rstc = mmio_read32(PM_RSTC);

    mmio_write32(PM_WDOG, PM_PASSWORD | PM_WDOG_TICKS);
    mmio_write32(PM_RSTC, PM_PASSWORD | (rstc & ~PM_RSTC_WRCFG_MASK) | PM_RSTC_FULL_RESET);

    for (;;) {
        arch_wait_for_event();
    }
}
