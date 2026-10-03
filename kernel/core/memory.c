/* RTR-OS - memory routines required by the compiler. */
#include "memory.h"

#include <stdint.h>

void *memcpy(void *destination, const void *source, size_t size)
{
    uint8_t *to = destination;
    const uint8_t *from = source;

    for (size_t i = 0U; i < size; i++) {
        to[i] = from[i];
    }
    return destination;
}

void *memset(void *destination, int value, size_t size)
{
    uint8_t *to = destination;

    for (size_t i = 0U; i < size; i++) {
        to[i] = (uint8_t)value;
    }
    return destination;
}
