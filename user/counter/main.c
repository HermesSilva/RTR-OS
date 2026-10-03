/*
 * RTR-OS - binary counter on consecutive GPIO pins (real-time program).
 *
 * Every activation the counter increments and its bits go to `width`
 * consecutive pins starting at `first`, bit 0 on the first pin. The lowest
 * pin toggles at every period, the next at every second period, and so on:
 * a test pattern that exercises many pins at once, for the bench. The
 * process must own the "gpio" device.
 *
 * Argument: (first << 8) | width. The default argument 0 means 16 pins from
 * GPIO 4 (GPIO 4 to 19).
 */
#include <stdbool.h>

#include <rtr/user.h>

#define GPIO_FSEL0          0x00U       /* function select, 10 pins per register */
#define GPIO_SET0           0x1CU
#define GPIO_CLR0           0x28U
#define FSEL_OUTPUT         1U
#define FIRST_DEFAULT       4U
#define WIDTH_DEFAULT       16U
#define WIDTH_MAX           24U
#define PIN_MAX             27U

int program_main(uint64_t argument);

static volatile uint32_t *gpio;

static void pin_output(uint32_t pin)
{
    uint32_t reg = (GPIO_FSEL0 / 4U) + (pin / 10U);
    uint32_t shift = (pin % 10U) * 3U;
    uint32_t value = gpio[reg];

    value &= ~(7U << shift);
    value |= FSEL_OUTPUT << shift;
    gpio[reg] = value;
}

static bool find_region(const char *name, struct rtr_region *out)
{
    for (uint64_t index = 0U; rtr_region_info(index, out) == RTR_OK; index++) {
        uint32_t i = 0U;

        while ((i < RTR_NAME_MAX) && (out->name[i] == name[i]) && (name[i] != '\0')) {
            i++;
        }
        if ((i < RTR_NAME_MAX) && (out->name[i] == name[i])) {
            return true;
        }
    }
    return false;
}

int program_main(uint64_t argument)
{
    struct rtr_region region;
    uint32_t first = (argument == 0U) ? FIRST_DEFAULT : (uint32_t)((argument >> 8U) & 0xFFU);
    uint32_t width = (argument == 0U) ? WIDTH_DEFAULT : (uint32_t)(argument & 0xFFU);
    uint32_t mask;
    uint32_t count = 0U;

    if (!find_region("gpio", &region) || (width == 0U) || (width > WIDTH_MAX) ||
        (first + width - 1U > PIN_MAX)) {
        rtr_print("counter: no gpio device, or bad first/width argument\n");
        return 1;
    }
    gpio = (volatile uint32_t *)(uintptr_t)region.address;
    mask = ((1U << width) - 1U) << first;   /* all pins are below 32 */
    for (uint32_t pin = first; pin < first + width; pin++) {
        pin_output(pin);
    }

    rtr_print("counter: ");
    rtr_print_dec(width);
    rtr_print(" pins from GPIO ");
    rtr_print_dec(first);
    rtr_print("\n");

    for (;;) {
        uint32_t value = (count << first) & mask;

        /* Set the ones, clear the zeros: two register writes per period. */
        gpio[GPIO_SET0 / 4U] = value;
        gpio[GPIO_CLR0 / 4U] = mask & ~value;
        count++;
        rtr_wait_period();
    }
}
