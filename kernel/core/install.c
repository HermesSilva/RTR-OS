/*
 * RTR-OS - the boot set.
 *
 * The installation is described by the manifest on the SD card (D3a, D12).
 * The kernel itself has no card driver (D5), so it starts a single built-in
 * process, `system`, which owns the card and the network: it reads the
 * manifest, asks the kernel to create the other processes, seals the
 * installation, and then serves the web interface.
 */
#include <stddef.h>

#include "board.h"
#include "console.h"
#include "process.h"

extern const uint8_t program_system_start[];
extern const uint8_t program_system_end[];

#define SYSTEM_DMA_SIZE 0x100000UL      /* 1 MB for network frame buffers */

void install_load(void)
{
    const struct process_config config = {
        .name = "system",
        .image = program_system_start,
        .image_size = (uint64_t)(uintptr_t)program_system_end - (uint64_t)(uintptr_t)program_system_start,
        .priority = 4U,
        .system = true,
        .period_us = 1000U,
        .limit_us = 800U,
        .floor_us = 400U,
        .stack_pages = 16U,
        .region_count = 3U,
        .regions = {
            { .name = "ethernet", .kind = RTR_REGION_DEVICE,
              .physical = BOARD_GENET_BASE, .size = BOARD_GENET_SIZE },
            { .name = "sdcard", .kind = RTR_REGION_DEVICE,
              .physical = BOARD_EMMC2_BASE, .size = BOARD_EMMC2_SIZE },
            { .name = "frames", .kind = RTR_REGION_DMA, .size = SYSTEM_DMA_SIZE },
        },
    };

    if (process_create(&config)) {
        console_put_text("boot set: process system loaded\n");
    } else {
        console_put_text("boot set: process system could not be loaded\n");
    }
}
