/*
 * RTR-OS - a real-time program: a periodic job with bounded work.
 *
 * Every period it does a fixed amount of computation, timed by the system
 * counter, and yields. It never blocks and never calls the kernel inside
 * the job: the pattern a real-time process has to follow.
 */
#include <rtr/user.h>

#define WORK_US 50U

int program_main(uint64_t argument);

int program_main(uint64_t argument)
{
    uint64_t ticks = (WORK_US * rtr_counter_hz()) / 1000000U;

    (void)argument;
    for (;;) {
        uint64_t end = rtr_counter() + ticks;

        while (rtr_counter() < end) {
        }
        rtr_wait_period();
    }
}
