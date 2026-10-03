/*
 * RTR-OS - memory routines.
 *
 * The compiler emits calls to memcpy and memset on its own (struct copies,
 * initialization), so they must exist under those names.
 */
#ifndef RTR_MEMORY_H
#define RTR_MEMORY_H

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);

#endif
