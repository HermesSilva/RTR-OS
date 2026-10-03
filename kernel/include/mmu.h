/* RTR-OS - address translation: kernel map and process address spaces. */
#ifndef RTR_MMU_H
#define RTR_MMU_H

#include <stdbool.h>
#include <stdint.h>

#define MMU_PAGE_SIZE 0x1000UL

/*
 * Builds the kernel map and turns on the MMU and the caches. Addresses do
 * not change (identity map); what takes effect are the permissions:
 *
 *   kernel code              read and execute
 *   constants                read only
 *   data, stack and RAM      read and write, no execute
 *   peripherals              read and write, no execute, no cache
 *
 * Covers the first gigabyte of RAM and the peripheral window; the rest of
 * the address space stays unmapped and any access to it is a fault.
 * Nothing of the kernel is reachable by a process.
 */
void mmu_init(void);

/* Page kind in a process address space. */
enum mmu_user_page {
    MMU_USER_CODE,                      /* read and execute */
    MMU_USER_CONST,                     /* read only */
    MMU_USER_DATA,                      /* read and write */
    MMU_USER_DEVICE,                    /* device registers */
    MMU_USER_DMA,                       /* read and write, no cache */
};

/* Creates an empty address space that already holds the kernel part. Returns 0 if out of memory. */
uint64_t mmu_space_create(void) __attribute__((warn_unused_result));

/* Maps one physical page at `address` in `space`. */
bool mmu_space_map(uint64_t space, uint64_t address, uint64_t physical, enum mmu_user_page kind)
    __attribute__((warn_unused_result));

/* Tells whether [address, address + size) is fully mapped for the process, writable if `write`. */
bool mmu_space_check(uint64_t space, uint64_t address, uint64_t size, bool write)
    __attribute__((warn_unused_result));

#ifdef RTR_FAULT_TEST
/* Only exists in the test build (CMake option RTR_FAULT_TEST). */
void mmu_fault_test(void);
#endif

#endif
