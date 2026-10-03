/*
 * RTR-OS - SD card driver (EMMC2 host of the BCM2711, SDHCI compatible).
 *
 * Runs inside the process, by programmed I/O: no interrupts, no DMA.
 */
#ifndef SYSTEM_SDCARD_H
#define SYSTEM_SDCARD_H

#include <stdbool.h>
#include <stdint.h>

#define SDCARD_BLOCK_SIZE   512U

/* Initializes the host and the card. `registers` is the host address in the process space. */
bool sdcard_init(uint64_t registers);

/* Card size in 512-byte blocks, 0 if there is no card. */
uint64_t sdcard_blocks(void);

bool sdcard_read(uint64_t block, uint8_t *buffer) __attribute__((warn_unused_result));
bool sdcard_write(uint64_t block, const uint8_t *buffer) __attribute__((warn_unused_result));

#endif
