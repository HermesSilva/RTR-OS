/*
 * RTR-OS - physical page reservation.
 *
 * Pages are handed out in sequence and never returned: all memory is
 * distributed at startup, and nothing is allocated after it (coding rule).
 */
#ifndef RTR_PAGE_H
#define RTR_PAGE_H

#include <stdint.h>

/* Sets the range pages come from; `reserved` is an address it must not cover. */
void page_init(uint64_t reserved);

/* Returns the physical address of `count` contiguous zeroed pages, or 0 if there are none. */
uint64_t page_alloc(uint64_t count) __attribute__((warn_unused_result));

uint64_t page_bytes_used(void);

#endif
