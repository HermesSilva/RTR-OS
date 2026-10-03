/*
 * RTR-OS - SD card driver (EMMC2 host of the BCM2711, SDHCI compatible).
 *
 * Single-block reads and writes, one command at a time, waiting on the
 * host status with bounded timeouts. Enough for the manifest; not meant for
 * throughput.
 */
#include "sdcard.h"

#include <string.h>

#include <rtr/user.h>

/* SDHCI registers (32-bit access only on this host) */
#define REG_ARGUMENT        0x08U
#define REG_BLOCK           0x04U       /* block size [15:0], block count [31:16] */
#define REG_COMMAND         0x0CU       /* transfer mode [15:0], command [31:16] */
#define REG_RESPONSE0       0x10U
#define REG_RESPONSE1       0x14U
#define REG_RESPONSE2       0x18U
#define REG_RESPONSE3       0x1CU
#define REG_DATA            0x20U
#define REG_STATE           0x24U
#define REG_CONTROL0        0x28U       /* host control, power control */
#define REG_CONTROL1        0x2CU       /* clock control, timeout, software reset */
#define REG_INTERRUPT       0x30U
#define REG_INTERRUPT_MASK  0x34U
#define REG_INTERRUPT_EN    0x38U
#define REG_CAPABILITIES    0x40U

#define STATE_CMD_INHIBIT   (1U << 0U)
#define STATE_DAT_INHIBIT   (1U << 1U)
#define STATE_CARD_INSERTED (1U << 16U)

#define CTRL0_POWER_ON_33V  0x0F00U     /* bus power on, 3.3 V */
#define CTRL1_CLK_INTERNAL  (1U << 0U)
#define CTRL1_CLK_STABLE    (1U << 1U)
#define CTRL1_CLK_ENABLE    (1U << 2U)
#define CTRL1_TIMEOUT_MAX   (0xEU << 16U)
#define CTRL1_RESET_ALL     (1U << 24U)
#define CTRL1_RESET_CMD     (1U << 25U)
#define CTRL1_RESET_DATA    (1U << 26U)

#define INT_COMMAND_DONE    (1U << 0U)
#define INT_TRANSFER_DONE   (1U << 1U)
#define INT_WRITE_READY     (1U << 4U)
#define INT_READ_READY      (1U << 5U)
#define INT_ERROR           (1U << 15U)
#define INT_ALL             0xFFFFFFFFU

/* Command register fields */
#define CMD_INDEX_SHIFT     24U
#define CMD_RESPONSE_NONE   (0U << 16U)
#define CMD_RESPONSE_136    (1U << 16U)
#define CMD_RESPONSE_48     (2U << 16U)
#define CMD_RESPONSE_48B    (3U << 16U)
#define CMD_CRC_CHECK       (1U << 19U)
#define CMD_INDEX_CHECK     (1U << 20U)
#define CMD_DATA            (1U << 21U)
#define TM_READ             (1U << 4U)

/* SD commands */
#define CMD_GO_IDLE         0U
#define CMD_ALL_SEND_CID    2U
#define CMD_SEND_RCA        3U
#define CMD_SELECT          7U
#define CMD_SEND_IF_COND    8U
#define CMD_SEND_CSD        9U
#define CMD_SET_BLOCKLEN    16U
#define CMD_READ_BLOCK      17U
#define CMD_WRITE_BLOCK     24U
#define CMD_APP             55U
#define ACMD_SEND_OP_COND   41U

#define IF_COND_ARGUMENT    0x000001AAU /* 2.7-3.6 V, check pattern 0xAA */
#define OP_COND_ARGUMENT    0x51FF8000U /* HCS, XPC, all voltage windows */
#define OCR_BUSY            (1U << 31U)
#define OCR_HIGH_CAPACITY   (1U << 30U)

#define CLOCK_IDENTIFY_HZ   400000U
#define CLOCK_DATA_HZ       25000000U
#define CLOCK_BASE_DEFAULT  100000000U
#define WORDS_PER_BLOCK     (SDCARD_BLOCK_SIZE / 4U)

#define TIMEOUT_COMMAND_US  100000U
#define TIMEOUT_DATA_US     500000U
#define TIMEOUT_INIT_US     1000000U
#define OP_COND_ATTEMPTS    100U

static volatile uint32_t *regs;
static uint32_t rca;
static bool high_capacity;
static uint64_t block_count;
static uint32_t response[4];

static uint32_t reg_read(uint32_t offset)
{
    return regs[offset / 4U];
}

static void reg_write(uint32_t offset, uint32_t value)
{
    regs[offset / 4U] = value;
}

static uint64_t deadline_after(uint64_t micros)
{
    return rtr_counter() + ((micros * rtr_counter_hz()) / 1000000U) + 1U;
}

static void delay_us(uint64_t micros)
{
    uint64_t end = deadline_after(micros);

    while (rtr_counter() < end) {
    }
}

/* Waits until (reg & mask) == value, or the time runs out. */
static bool wait_register(uint32_t offset, uint32_t mask, uint32_t value, uint64_t micros)
{
    uint64_t end = deadline_after(micros);

    while ((reg_read(offset) & mask) != value) {
        if (rtr_counter() > end) {
            return false;
        }
    }
    return true;
}

static bool wait_interrupt(uint32_t bit, uint64_t micros)
{
    uint64_t end = deadline_after(micros);

    for (;;) {
        uint32_t status = reg_read(REG_INTERRUPT);

        if ((status & INT_ERROR) != 0U) {
            reg_write(REG_INTERRUPT, status);
            return false;
        }
        if ((status & bit) != 0U) {
            reg_write(REG_INTERRUPT, bit);
            return true;
        }
        if (rtr_counter() > end) {
            return false;
        }
    }
}

static void reset_lines(void)
{
    reg_write(REG_CONTROL1, reg_read(REG_CONTROL1) | CTRL1_RESET_CMD | CTRL1_RESET_DATA);
    (void)wait_register(REG_CONTROL1, CTRL1_RESET_CMD | CTRL1_RESET_DATA, 0U, TIMEOUT_COMMAND_US);
}

/* Sends a command; `flags` carries the response type and the data bit. */
static bool command(uint32_t index, uint32_t argument, uint32_t flags)
{
    if (!wait_register(REG_STATE, STATE_CMD_INHIBIT, 0U, TIMEOUT_COMMAND_US)) {
        return false;
    }
    if (((flags & CMD_DATA) != 0U) || ((flags & CMD_RESPONSE_48B) == CMD_RESPONSE_48B)) {
        if (!wait_register(REG_STATE, STATE_DAT_INHIBIT, 0U, TIMEOUT_DATA_US)) {
            return false;
        }
    }

    reg_write(REG_INTERRUPT, INT_ALL);
    reg_write(REG_ARGUMENT, argument);
    reg_write(REG_COMMAND, (index << CMD_INDEX_SHIFT) | flags);

    if (!wait_interrupt(INT_COMMAND_DONE, TIMEOUT_COMMAND_US)) {
        reset_lines();
        return false;
    }

    response[0] = reg_read(REG_RESPONSE0);
    response[1] = reg_read(REG_RESPONSE1);
    response[2] = reg_read(REG_RESPONSE2);
    response[3] = reg_read(REG_RESPONSE3);
    return true;
}

static bool app_command(uint32_t index, uint32_t argument, uint32_t flags)
{
    return command(CMD_APP, rca << 16U, CMD_RESPONSE_48 | CMD_CRC_CHECK | CMD_INDEX_CHECK) &&
           command(index, argument, flags);
}

static bool set_clock(uint32_t target_hz)
{
    uint32_t base_hz = ((reg_read(REG_CAPABILITIES) >> 8U) & 0xFFU) * 1000000U;
    uint32_t divisor = 0U;
    uint32_t control;

    if (base_hz == 0U) {
        base_hz = CLOCK_BASE_DEFAULT;
    }
    /* SDHCI v3: clock = base / (2 * divisor), divisor up to 1023 */
    if (target_hz < base_hz) {
        divisor = (base_hz + (2U * target_hz) - 1U) / (2U * target_hz);
        if (divisor > 1023U) {
            divisor = 1023U;
        }
    }

    control = reg_read(REG_CONTROL1) & ~(CTRL1_CLK_ENABLE | 0xFFC0U);
    reg_write(REG_CONTROL1, control);
    delay_us(10U);

    control |= CTRL1_CLK_INTERNAL | ((divisor & 0xFFU) << 8U) | (((divisor >> 8U) & 3U) << 6U);
    reg_write(REG_CONTROL1, control);
    if (!wait_register(REG_CONTROL1, CTRL1_CLK_STABLE, CTRL1_CLK_STABLE, TIMEOUT_COMMAND_US)) {
        return false;
    }
    reg_write(REG_CONTROL1, reg_read(REG_CONTROL1) | CTRL1_CLK_ENABLE);
    delay_us(10U);
    return true;
}

/*
 * Card capacity, from the CSD register (versions 1 and 2). The host strips
 * the CRC byte, so response word n holds CSD bits 32n+39 down to 32n+8.
 */
static uint64_t capacity_from_csd(void)
{
    uint32_t version = (response[3] >> 22U) & 3U;   /* CSD_STRUCTURE, bits 127:126 */

    if (version == 1U) {
        /* CSD v2: C_SIZE in bits 69:48; size = (C_SIZE + 1) * 512 KiB */
        uint64_t c_size = (response[1] >> 8U) & 0x3FFFFFU;

        return (c_size + 1U) * 1024U;
    } else {
        /* CSD v1: C_SIZE in bits 73:62, C_SIZE_MULT in 49:47, READ_BL_LEN in 83:80 */
        uint64_t c_size = ((uint64_t)(response[2] & 3U) << 10U) | (response[1] >> 22U);
        uint32_t mult = (response[1] >> 7U) & 7U;
        uint32_t block_len = (response[2] >> 8U) & 0xFU;
        uint64_t bytes = (c_size + 1U) << (mult + 2U + block_len);

        return bytes / SDCARD_BLOCK_SIZE;
    }
}

bool sdcard_init(uint64_t registers)
{
    uint32_t ocr = 0U;

    if (registers == 0U) {
        return false;
    }
    regs = (volatile uint32_t *)(uintptr_t)registers;
    block_count = 0U;

    reg_write(REG_CONTROL1, reg_read(REG_CONTROL1) | CTRL1_RESET_ALL);
    if (!wait_register(REG_CONTROL1, CTRL1_RESET_ALL, 0U, TIMEOUT_INIT_US)) {
        return false;
    }

    reg_write(REG_CONTROL0, CTRL0_POWER_ON_33V);
    delay_us(1000U);
    if ((reg_read(REG_STATE) & STATE_CARD_INSERTED) == 0U) {
        return false;
    }

    reg_write(REG_CONTROL1, reg_read(REG_CONTROL1) | CTRL1_TIMEOUT_MAX);
    if (!set_clock(CLOCK_IDENTIFY_HZ)) {
        return false;
    }
    reg_write(REG_INTERRUPT_EN, 0U);            /* no interrupt signals: everything by polling */
    reg_write(REG_INTERRUPT_MASK, INT_ALL);
    reg_write(REG_INTERRUPT, INT_ALL);

    if (!command(CMD_GO_IDLE, 0U, CMD_RESPONSE_NONE)) {
        return false;
    }
    delay_us(1000U);
    if (!command(CMD_SEND_IF_COND, IF_COND_ARGUMENT, CMD_RESPONSE_48 | CMD_CRC_CHECK | CMD_INDEX_CHECK) ||
        ((response[0] & 0xFFU) != 0xAAU)) {
        return false;                           /* only SD version 2 cards are supported */
    }

    rca = 0U;
    for (uint32_t attempt = 0U; attempt < OP_COND_ATTEMPTS; attempt++) {
        if (!app_command(ACMD_SEND_OP_COND, OP_COND_ARGUMENT, CMD_RESPONSE_48)) {
            return false;
        }
        ocr = response[0];
        if ((ocr & OCR_BUSY) != 0U) {
            break;
        }
        delay_us(10000U);
    }
    if ((ocr & OCR_BUSY) == 0U) {
        return false;
    }
    high_capacity = (ocr & OCR_HIGH_CAPACITY) != 0U;

    if (!command(CMD_ALL_SEND_CID, 0U, CMD_RESPONSE_136 | CMD_CRC_CHECK) ||
        !command(CMD_SEND_RCA, 0U, CMD_RESPONSE_48 | CMD_CRC_CHECK | CMD_INDEX_CHECK)) {
        return false;
    }
    rca = response[0] >> 16U;

    if (!command(CMD_SEND_CSD, rca << 16U, CMD_RESPONSE_136 | CMD_CRC_CHECK)) {
        return false;
    }
    block_count = capacity_from_csd();

    if (!command(CMD_SELECT, rca << 16U, CMD_RESPONSE_48B | CMD_CRC_CHECK | CMD_INDEX_CHECK) ||
        !set_clock(CLOCK_DATA_HZ) ||
        !command(CMD_SET_BLOCKLEN, SDCARD_BLOCK_SIZE, CMD_RESPONSE_48 | CMD_CRC_CHECK | CMD_INDEX_CHECK)) {
        block_count = 0U;
        return false;
    }
    return true;
}

uint64_t sdcard_blocks(void)
{
    return block_count;
}

static uint32_t block_argument(uint64_t block)
{
    return high_capacity ? (uint32_t)block : (uint32_t)(block * SDCARD_BLOCK_SIZE);
}

bool sdcard_read(uint64_t block, uint8_t *buffer)
{
    if ((buffer == NULL) || (block_count == 0U) || (block >= block_count)) {
        return false;
    }

    reg_write(REG_BLOCK, (1U << 16U) | SDCARD_BLOCK_SIZE);
    if (!command(CMD_READ_BLOCK, block_argument(block), CMD_RESPONSE_48 | CMD_CRC_CHECK |
                 CMD_INDEX_CHECK | CMD_DATA | TM_READ)) {
        return false;
    }
    if (!wait_interrupt(INT_READ_READY, TIMEOUT_DATA_US)) {
        reset_lines();
        return false;
    }
    for (uint32_t i = 0U; i < WORDS_PER_BLOCK; i++) {
        uint32_t word = reg_read(REG_DATA);

        (void)memcpy(&buffer[i * 4U], &word, sizeof(word));
    }
    if (!wait_interrupt(INT_TRANSFER_DONE, TIMEOUT_DATA_US)) {
        reset_lines();
        return false;
    }
    return true;
}

bool sdcard_write(uint64_t block, const uint8_t *buffer)
{
    if ((buffer == NULL) || (block_count == 0U) || (block >= block_count)) {
        return false;
    }

    reg_write(REG_BLOCK, (1U << 16U) | SDCARD_BLOCK_SIZE);
    if (!command(CMD_WRITE_BLOCK, block_argument(block), CMD_RESPONSE_48 | CMD_CRC_CHECK |
                 CMD_INDEX_CHECK | CMD_DATA)) {
        return false;
    }
    if (!wait_interrupt(INT_WRITE_READY, TIMEOUT_DATA_US)) {
        reset_lines();
        return false;
    }
    for (uint32_t i = 0U; i < WORDS_PER_BLOCK; i++) {
        uint32_t word;

        (void)memcpy(&word, &buffer[i * 4U], sizeof(word));
        reg_write(REG_DATA, word);
    }
    if (!wait_interrupt(INT_TRANSFER_DONE, TIMEOUT_DATA_US)) {
        reset_lines();
        return false;
    }
    return true;
}
