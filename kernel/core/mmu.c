/* RTR-OS - tradução de endereços do kernel (AArch64, páginas de 4 KB, 39 bits). */
#include "mmu.h"

#include <stdint.h>

#include "arch.h"
#include "board.h"
#include "panic.h"

#define ENTRIES             512U
#define PAGE_SIZE           0x1000UL
#define BLOCK_2M            0x200000UL
#define BLOCK_1G            0x40000000UL

/* Tipo do descritor */
#define DESC_BLOCK          0x1UL       /* níveis 1 e 2 */
#define DESC_TABLE          0x3UL       /* níveis 1 e 2 */
#define DESC_PAGE           0x3UL       /* nível 3 */

/* Atributos */
#define ATTR_INDEX_RAM      (0UL << 2U)
#define ATTR_INDEX_DEVICE   (1UL << 2U)
#define ATTR_READ_ONLY      (2UL << 6U)
#define ATTR_SHARED_INNER   (3UL << 8U)
#define ATTR_ACCESSED       (1UL << 10U)
#define ATTR_NO_EXEC_EL1    (1UL << 53U)
#define ATTR_NO_EXEC_EL0    (1UL << 54U)

#define RAM_COMMON          (ATTR_INDEX_RAM | ATTR_SHARED_INNER | ATTR_ACCESSED | ATTR_NO_EXEC_EL0)
#define MAP_CODE            (RAM_COMMON | ATTR_READ_ONLY)
#define MAP_CONST           (RAM_COMMON | ATTR_READ_ONLY | ATTR_NO_EXEC_EL1)
#define MAP_DATA            (RAM_COMMON | ATTR_NO_EXEC_EL1)
#define MAP_DEVICE          (ATTR_INDEX_DEVICE | ATTR_ACCESSED | ATTR_NO_EXEC_EL1 | ATTR_NO_EXEC_EL0)

/* MAIR: índice 0 = memória normal com cache; índice 1 = dispositivo (nGnRE) */
#define MAIR_VALUE          (0xFFUL | (0x04UL << 8U))

/* TCR: 39 bits em TTBR0, páginas de 4 KB, tabelas em memória com cache, TTBR1 desligado, PA de 36 bits */
#define TCR_T0SZ_39BIT      25UL
#define TCR_IRGN0_CACHED    (1UL << 8U)
#define TCR_ORGN0_CACHED    (1UL << 10U)
#define TCR_SH0_INNER       (3UL << 12U)
#define TCR_EPD1            (1UL << 23U)
#define TCR_IPS_36BIT       (1UL << 32U)
#define TCR_VALUE           (TCR_T0SZ_39BIT | TCR_IRGN0_CACHED | TCR_ORGN0_CACHED | \
                             TCR_SH0_INNER | TCR_EPD1 | TCR_IPS_36BIT)

/* SCTLR: MMU, cache de dados e de instruções, checagem de pilha, e "gravável nunca executa" */
#define SCTLR_RES1          0x30D00800UL
#define SCTLR_MMU           (1UL << 0U)
#define SCTLR_DCACHE        (1UL << 2U)
#define SCTLR_STACK_ALIGN   (1UL << 3U)
#define SCTLR_ICACHE        (1UL << 12U)
#define SCTLR_WXN           (1UL << 19U)
#define SCTLR_VALUE         (SCTLR_RES1 | SCTLR_MMU | SCTLR_DCACHE | SCTLR_STACK_ALIGN | \
                             SCTLR_ICACHE | SCTLR_WXN)

/* Limites das seções da imagem, definidos em linker.ld e alinhados a página. */
extern const uint8_t image_text_start[];
extern const uint8_t image_text_end[];
extern const uint8_t image_rodata_end[];
extern const uint8_t image_end[];

static uint64_t level1[ENTRIES] __attribute__((aligned(4096)));
static uint64_t level2_ram[ENTRIES] __attribute__((aligned(4096)));
static uint64_t level2_device[ENTRIES] __attribute__((aligned(4096)));
static uint64_t level3_kernel[ENTRIES] __attribute__((aligned(4096)));

#ifdef RTR_FAULT_TEST
/* Ensaio de proteção: grava no código do kernel. Com a MMU ligada, tem de falhar. */
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
    const uint64_t device_gigabyte = BOARD_DEVICE_START / BLOCK_1G;
    const uint64_t device_base = device_gigabyte * BLOCK_1G;

    /* A imagem inteira precisa caber nos 2 MB cobertos pela tabela de páginas do kernel. */
    if ((uint64_t)(uintptr_t)image_end > BLOCK_2M) {
        kernel_panic("imagem do kernel maior que 2 MB");
    }

    /* Primeiros 2 MB, página a página, com a permissão de cada seção da imagem. */
    for (uint64_t i = 0U; i < ENTRIES; i++) {
        uint64_t address = i * PAGE_SIZE;
        uint64_t attributes = MAP_DATA;

        if ((address >= text_start) && (address < text_end)) {
            attributes = MAP_CODE;
        } else if ((address >= text_end) && (address < rodata_end)) {
            attributes = MAP_CONST;
        } else {
            /* dados do kernel e memória livre */
        }
        level3_kernel[i] = address | attributes | DESC_PAGE;
    }

    /* Restante do primeiro gigabyte de RAM, em blocos de 2 MB. */
    level2_ram[0] = (uint64_t)(uintptr_t)level3_kernel | DESC_TABLE;
    for (uint64_t i = 1U; i < ENTRIES; i++) {
        level2_ram[i] = (i * BLOCK_2M) | MAP_DATA | DESC_BLOCK;
    }

    /* Janela dos periféricos, no fim do quarto gigabyte. */
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
    level1[device_gigabyte] = (uint64_t)(uintptr_t)level2_device | DESC_TABLE;

    arch_mmu_enable((uint64_t)(uintptr_t)level1, TCR_VALUE, MAIR_VALUE, SCTLR_VALUE);
}
