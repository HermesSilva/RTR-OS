/* RTR-OS - physical page reservation. */
#include "page.h"

#include "arch.h"
#include "memory.h"
#include "mmu.h"
#include "panic.h"

/*
 * The pool runs from the end of the kernel image to 64 MB. It stays inside
 * the first gigabyte, which the kernel sees through the identity map.
 */
#define POOL_END 0x04000000UL

extern const uint8_t image_end[];

static uint64_t pool_next;

void page_init(uint64_t reserved)
{
    pool_next = (uint64_t)(uintptr_t)image_end;

    /* The device tree handed over by the firmware must not be inside the pool. */
    if ((reserved >= pool_next) && (reserved < POOL_END)) {
        kernel_panic("device tree inside the memory pool");
    }
}

uint64_t page_alloc(uint64_t count)
{
    uint64_t size = count * MMU_PAGE_SIZE;
    uint64_t start = pool_next;

    if ((count == 0U) || (pool_next == 0U) || (size > (POOL_END - pool_next))) {
        return 0U;
    }

    pool_next += size;
    (void)memset(arch_physical_pointer(start), 0, size);
    return start;
}

uint64_t page_bytes_used(void)
{
    return pool_next - (uint64_t)(uintptr_t)image_end;
}
