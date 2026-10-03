/* RTR-OS - process creation from a program image. */
#ifndef RTR_PROCESS_H
#define RTR_PROCESS_H

#include <stdbool.h>
#include <stdint.h>

#include "sched.h"

struct process_region {
    const char *name;
    uint32_t kind;                      /* RTR_REGION_DEVICE or RTR_REGION_DMA */
    uint64_t physical;                  /* devices only */
    uint64_t size;
};

/* Process description: what the manifest will say once it exists (D3a, D12). */
struct process_config {
    const char *name;
    const uint8_t *image;
    uint64_t image_size;
    uint64_t argument;                  /* handed to the program in x0 */
    uint32_t priority;
    bool system;
    bool realtime;                      /* real-time process: the program must allow it */
    uint64_t period_us;
    uint64_t limit_us;                  /* 0 = default limit */
    uint64_t floor_us;
    uint64_t stack_pages;
    uint32_t region_count;
    struct process_region regions[SCHED_REGIONS_MAX];
};

/* Creates the process and its task. On error, reports on the console and returns false. */
bool process_create(const struct process_config *config) __attribute__((warn_unused_result));

/*
 * Creates a process from a manifest description and a program image read
 * by the boot set; the devices are looked up by name. On error, reports on
 * the console and returns false.
 */
bool process_create_from_spec(const struct rtr_process_spec *spec, const uint8_t *image,
                              uint64_t image_size) __attribute__((warn_unused_result));

/* Checks that a name from a process is NUL-terminated within RTR_NAME_MAX. */
bool process_name_valid(const char *name) __attribute__((warn_unused_result));

/*
 * Loads the boot set: the system process, which reads the manifest from the
 * SD card and asks the kernel to create the other processes.
 */
void install_load(void);

#endif
