/* RTR-OS - address translation (AArch64, 4 KB pages, 39 bits). */
#include "mmu.h"

#include <stddef.h>
#include <stdint.h>

#include "arch.h"
#include "board.h"
#include "page.h"
#include "panic.h"
#include "rtr/abi.h"

#define ENTRIES             512U
#define INDEX_MASK          0x1FFUL
#define BLOCK_2M            0x200000UL
#define BLOCK_1G            0x40000000UL
#define ADDRESS_MASK        0x0000FFFFFFFFF000UL

#define LEVEL1_SHIFT        30U
#define LEVEL2_SHIFT        21U
#define LEVEL3_SHIFT        12U

/* Descriptor type */
#define DESC_VALID          0x1UL
#define DESC_BLOCK          0x1UL       /* levels 1 and 2 */
#define DESC_TABLE          0x3UL       /* levels 1 and 2 */
#define DESC_PAGE           0x3UL       /* level 3 */

/* Attributes */
#define ATTR_INDEX_RAM      (0UL << 2U)
#define ATTR_INDEX_DEVICE   (1UL << 2U)
#define ATTR_INDEX_UNCACHED (2UL << 2U)
#define ATTR_USER           (1UL << 6U) /* reachable from EL0 */
#define ATTR_READ_ONLY      (2UL << 6U)
#define ATTR_SHARED_INNER   (3UL << 8U)
#define ATTR_ACCESSED       (1UL << 10U)
#define ATTR_NOT_GLOBAL     (1UL << 11U)
#define ATTR_NO_EXEC_EL1    (1UL << 53U)
#define ATTR_NO_EXEC_EL0    (1UL << 54U)

#define RAM_COMMON          (ATTR_INDEX_RAM | ATTR_SHARED_INNER | ATTR_ACCESSED | ATTR_NO_EXEC_EL0)
#define MAP_CODE            (RAM_COMMON | ATTR_READ_ONLY)
#define MAP_CONST           (RAM_COMMON | ATTR_READ_ONLY | ATTR_NO_EXEC_EL1)
#define MAP_DATA            (RAM_COMMON | ATTR_NO_EXEC_EL1)
#define MAP_DEVICE          (ATTR_INDEX_DEVICE | ATTR_ACCESSED | ATTR_NO_EXEC_EL1 | ATTR_NO_EXEC_EL0)

/* Process pages: never executable by the kernel, and outside the global map. */
#define USER_COMMON         (ATTR_USER | ATTR_ACCESSED | ATTR_NOT_GLOBAL | ATTR_NO_EXEC_EL1)
#define USER_RAM            (USER_COMMON | ATTR_INDEX_RAM | ATTR_SHARED_INNER)
#define MAP_USER_CODE       (USER_RAM | ATTR_READ_ONLY)
#define MAP_USER_CONST      (USER_RAM | ATTR_READ_ONLY | ATTR_NO_EXEC_EL0)
#define MAP_USER_DATA       (USER_RAM | ATTR_NO_EXEC_EL0)
#define MAP_USER_DEVICE     (USER_COMMON | ATTR_INDEX_DEVICE | ATTR_NO_EXEC_EL0)
#define MAP_USER_DMA        (USER_COMMON | ATTR_INDEX_UNCACHED | ATTR_SHARED_INNER | ATTR_NO_EXEC_EL0)

/* MAIR: 0 = normal cached memory; 1 = device (nGnRE); 2 = normal uncached memory */
#define MAIR_VALUE          (0xFFUL | (0x04UL << 8U) | (0x44UL << 16U))

/* TCR: 39 bits in TTBR0, 4 KB pages, cached table walks, TTBR1 off, 36-bit PA */
#define TCR_T0SZ_39BIT      25UL
#define TCR_IRGN0_CACHED    (1UL << 8U)
#define TCR_ORGN0_CACHED    (1UL << 10U)
#define TCR_SH0_INNER       (3UL << 12U)
#define TCR_EPD1            (1UL << 23U)
#define TCR_IPS_36BIT       (1UL << 32U)
#define TCR_VALUE           (TCR_T0SZ_39BIT | TCR_IRGN0_CACHED | TCR_ORGN0_CACHED | \
                             TCR_SH0_INNER | TCR_EPD1 | TCR_IPS_36BIT)

/* SCTLR: MMU, caches, stack alignment checks at both levels, and "writable never executes" */
#define SCTLR_RES1          0x30D00800UL
#define SCTLR_MMU           (1UL << 0U)
#define SCTLR_DCACHE        (1UL << 2U)
#define SCTLR_STACK_ALIGN   (1UL << 3U)
#define SCTLR_STACK_ALIGN0  (1UL << 4U)
#define SCTLR_ICACHE        (1UL << 12U)
#define SCTLR_WXN           (1UL << 19U)
#define SCTLR_VALUE         (SCTLR_RES1 | SCTLR_MMU | SCTLR_DCACHE | SCTLR_STACK_ALIGN | \
                             SCTLR_STACK_ALIGN0 | SCTLR_ICACHE | SCTLR_WXN)

/* Image section bounds, defined in linker.ld and page aligned. */
extern const uint8_t image_text_start[];
extern const uint8_t image_text_end[];
extern const uint8_t image_rodata_end[];
extern const uint8_t image_end[];

static uint64_t level1[ENTRIES] __attribute__((aligned(4096)));
static uint64_t level2_ram[ENTRIES] __attribute__((aligned(4096)));
static uint64_t level2_device[ENTRIES] __attribute__((aligned(4096)));
static uint64_t level3_kernel[ENTRIES] __attribute__((aligned(4096)));

static uint64_t device_gigabyte(void)
{
    return BOARD_DEVICE_START / BLOCK_1G;
}

#ifdef RTR_FAULT_TEST
/* Protection test: writes to kernel code. With the MMU on, it must fail. */
void mmu_fault_test(void)
{
    volatile uint8_t *code = (volatile uint8_t *)(uintptr_t)image_text_start;

    *code = 0U;
}
#endif

void mmu_init(void)
{
    const uint64_t text_start = (uint64_t)(uintptr_t)image_text_start;
    const uint64_t text_end = (uint64_t)(uintptr_t)image_text_end;
    const uint64_t rodata_end = (uint64_t)(uintptr_t)image_rodata_end;
    const uint64_t device_base = device_gigabyte() * BLOCK_1G;

    /* The whole image must fit in the 2 MB covered by the kernel page table. */
    if ((uint64_t)(uintptr_t)image_end > BLOCK_2M) {
        kernel_panic("kernel image larger than 2 MB");
    }

    /* First 2 MB, page by page, with the permission of each image section. */
    for (uint64_t i = 0U; i < ENTRIES; i++) {
        uint64_t address = i * MMU_PAGE_SIZE;
        uint64_t attributes = MAP_DATA;

        if ((address >= text_start) && (address < text_end)) {
            attributes = MAP_CODE;
        } else if ((address >= text_end) && (address < rodata_end)) {
            attributes = MAP_CONST;
        } else {
            /* kernel data and free memory */
        }
        level3_kernel[i] = address | attributes | DESC_PAGE;
    }

    /* Rest of the first gigabyte of RAM, in 2 MB blocks. */
    level2_ram[0] = (uint64_t)(uintptr_t)level3_kernel | DESC_TABLE;
    for (uint64_t i = 1U; i < ENTRIES; i++) {
        level2_ram[i] = (i * BLOCK_2M) | MAP_DATA | DESC_BLOCK;
    }

    /* Peripheral window, at the end of the fourth gigabyte. */
    for (uint64_t i = 0U; i < ENTRIES; i++) {
        uint64_t address = device_base + (i * BLOCK_2M);

        if ((address >= BOARD_DEVICE_START) && (address < BOARD_DEVICE_END)) {
            level2_device[i] = address | MAP_DEVICE | DESC_BLOCK;
        } else {
            level2_device[i] = 0U;
        }
    }

    for (uint64_t i = 0U; i < ENTRIES; i++) {
        level1[i] = 0U;
    }
    level1[0] = (uint64_t)(uintptr_t)level2_ram | DESC_TABLE;
    level1[device_gigabyte()] = (uint64_t)(uintptr_t)level2_device | DESC_TABLE;

    arch_mmu_enable((uint64_t)(uintptr_t)level1, TCR_VALUE, MAIR_VALUE, SCTLR_VALUE);
}

static bool is_user_address(uint64_t address)
{
    return (address >= RTR_USER_BASE) && (address < RTR_USER_END);
}

static uint64_t *table_pointer(uint64_t descriptor)
{
    return arch_physical_pointer(descriptor & ADDRESS_MASK);
}

/* Returns the next-level table pointed to by `entry`, creating it if `create`. */
static uint64_t *next_table(uint64_t *entry, bool create)
{
    if ((*entry & DESC_VALID) == 0U) {
        uint64_t page;

        if (!create) {
            return NULL;
        }
        page = page_alloc(1U);
        if (page == 0U) {
            return NULL;
        }
        *entry = page | DESC_TABLE;
    }
    return table_pointer(*entry);
}

/* Returns the level 3 entry of `address`, or NULL if the tables do not exist. */
static uint64_t *page_entry(uint64_t space, uint64_t address, bool create)
{
    uint64_t *table1 = arch_physical_pointer(space);
    uint64_t *table2;
    uint64_t *table3;

    table2 = next_table(&table1[(address >> LEVEL1_SHIFT) & INDEX_MASK], create);
    if (table2 == NULL) {
        return NULL;
    }
    table3 = next_table(&table2[(address >> LEVEL2_SHIFT) & INDEX_MASK], create);
    if (table3 == NULL) {
        return NULL;
    }
    return &table3[(address >> LEVEL3_SHIFT) & INDEX_MASK];
}

uint64_t mmu_space_create(void)
{
    uint64_t space = page_alloc(1U);

    if (space != 0U) {
        uint64_t *table1 = arch_physical_pointer(space);

        /* The kernel part is the same in every space, and unreachable from EL0. */
        table1[0] = level1[0];
        table1[device_gigabyte()] = level1[device_gigabyte()];
    }
    return space;
}

bool mmu_space_map(uint64_t space, uint64_t address, uint64_t physical, enum mmu_user_page kind)
{
    uint64_t attributes;
    uint64_t *entry;

    if ((space == 0U) || !is_user_address(address) ||
        ((address % MMU_PAGE_SIZE) != 0U) || ((physical % MMU_PAGE_SIZE) != 0U)) {
        return false;
    }

    switch (kind) {
    case MMU_USER_CODE:
        attributes = MAP_USER_CODE;
        break;
    case MMU_USER_CONST:
        attributes = MAP_USER_CONST;
        break;
    case MMU_USER_DATA:
        attributes = MAP_USER_DATA;
        break;
    case MMU_USER_DEVICE:
        attributes = MAP_USER_DEVICE;
        break;
    case MMU_USER_DMA:
        attributes = MAP_USER_DMA;
        break;
    default:
        return false;
    }

    entry = page_entry(space, address, true);
    if ((entry == NULL) || ((*entry & DESC_VALID) != 0U)) {
        return false;                   /* out of memory, or address already taken */
    }
    *entry = physical | attributes | DESC_PAGE;
    return true;
}

bool mmu_space_check(uint64_t space, uint64_t address, uint64_t size, bool write)
{
    uint64_t first;
    uint64_t last;

    if ((space == 0U) || (size == 0U) || !is_user_address(address) ||
        (size > (RTR_USER_END - address))) {
        return false;
    }

    first = address / MMU_PAGE_SIZE;
    last = (address + size - 1U) / MMU_PAGE_SIZE;

    /* The loop is bounded by the size, which the caller already restricted. */
    for (uint64_t page = first; page <= last; page++) {
        const uint64_t *entry = page_entry(space, page * MMU_PAGE_SIZE, false);

        if ((entry == NULL) || ((*entry & DESC_VALID) == 0U) || ((*entry & ATTR_USER) == 0U)) {
            return false;
        }
        if (write && ((*entry & ATTR_READ_ONLY) != 0U)) {
            return false;
        }
    }
    return true;
}
