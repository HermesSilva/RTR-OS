/*
 * RTR-OS - the installation manifest.
 *
 * A plain-text file on the SD card, MANIFEST.TXT, read once at boot. It
 * describes every process: program, scheduling parameters and the devices
 * and memory it may use. Format:
 *
 *     # comment
 *     version 1
 *
 *     process report
 *       program demo
 *       argument 0
 *       priority 1
 *       period 5 s
 *       limit 200 ms
 *       system no
 *       floor 0
 *       stack 4
 *       device ethernet
 *       dma frames 1024 KiB
 *     end
 *
 * Time values take the units us, ms or s; memory sizes take KiB or MiB.
 * The entry named "system" configures the boot set itself.
 *
 * A real-time process (told by its program) has no priority, limit, system
 * flag or floor: it declares core N, the dedicated core it gets, and its
 * period. Until dedicated cores exist, the system runs it on core 0 ahead of
 * every standard process, with an implicit limit of half its period.
 */
#ifndef SYSTEM_MANIFEST_H
#define SYSTEM_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rtr/abi.h>

#define MANIFEST_PROCESSES_MAX  (RTR_STATS_TASKS_MAX - 1U)
#define MANIFEST_TEXT_MAX       8192U
#define MANIFEST_ERROR_MAX      96U

struct manifest {
    uint32_t count;
    struct rtr_process_spec processes[MANIFEST_PROCESSES_MAX];
};

/* Parses `text`. On failure returns false and fills `error` (and the line number). */
bool manifest_parse(const char *text, size_t size, struct manifest *out,
                    char *error, size_t error_capacity, uint32_t *line);

/* Writes the manifest back as text. Returns the size, or 0 if it did not fit. */
size_t manifest_serialize(const struct manifest *manifest, char *text, size_t capacity);

/* The manifest used when the card has none. */
const char *manifest_default_text(void);

#endif
