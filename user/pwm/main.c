/*
 * RTR-OS - software PWM modulator (real-time program).
 *
 * Drives one GPIO pin from software, timed by the system counter. The PWM
 * period is the process period from the manifest; every activation the
 * pin goes high, stays high for the duty time and goes low. The process
 * must own the "gpio" device.
 *
 * Argument: (pin << 8) | duty, with duty in percent (0..100). The default
 * argument 0 means pin 18 at 50%.
 */
#include <stdbool.h>

#include <rtr/user.h>

#define GPIO_FSEL0          0x00U       /* function select, 10 pins per register */
#define GPIO_SET0           0x1CU
#define GPIO_CLR0           0x28U
#define FSEL_OUTPUT         1U
#define PIN_DEFAULT         18U
#define DUTY_DEFAULT        50U
#define PIN_MAX             53U

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
    uint32_t pin = (argument == 0U) ? PIN_DEFAULT : (uint32_t)((argument >> 8U) & 0xFFU);
    uint32_t duty = (argument == 0U) ? DUTY_DEFAULT : (uint32_t)(argument & 0xFFU);
    uint32_t set_reg;
    uint32_t clear_reg;
    uint32_t bit;
    uint64_t high_ticks = 0U;
    uint64_t last_activation = 0U;
    uint64_t period_ticks = 0U;

    if (!find_region("gpio", &region) || (pin > PIN_MAX) || (duty > 100U)) {
        rtr_print("pwm: no gpio device, or bad pin/duty argument\n");
        return 1;
    }
    gpio = (volatile uint32_t *)(uintptr_t)region.address;
    set_reg = (GPIO_SET0 / 4U) + (pin / 32U);
    clear_reg = (GPIO_CLR0 / 4U) + (pin / 32U);
    bit = 1U << (pin % 32U);
    pin_output(pin);

    rtr_print("pwm: pin ");
    rtr_print_dec(pin);
    rtr_print(", duty ");
    rtr_print_dec(duty);
    rtr_print("%\n");

    for (;;) {
        uint64_t now = rtr_counter();

        /* The PWM period is the process period: measured from two activations. */
        if (last_activation != 0U) {
            period_ticks = now - last_activation;
            high_ticks = (period_ticks * duty) / 100U;
        }
        last_activation = now;

        if (high_ticks != 0U) {
            uint64_t end = now + high_ticks;

            gpio[set_reg] = bit;
            while (rtr_counter() < end) {
            }
        }
        gpio[clear_reg] = bit;
        rtr_wait_period();
    }
}
