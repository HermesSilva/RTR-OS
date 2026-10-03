/* RTR-OS - part of the C standard library offered to programs: memory and text. */
#include <string.h>

#include <stdint.h>
#include <stdlib.h>

int atoi(const char *text)
{
    size_t i = 0U;
    int value = 0;
    int sign = 1;

    if ((text[0] == '-') || (text[0] == '+')) {
        sign = (text[0] == '-') ? -1 : 1;
        i = 1U;
    }
    while ((text[i] >= '0') && (text[i] <= '9')) {
        value = (value * 10) + (text[i] - '0');
        i++;
    }
    return sign * value;
}

void *memcpy(void *destination, const void *source, size_t size)
{
    uint8_t *to = destination;
    const uint8_t *from = source;

    for (size_t i = 0U; i < size; i++) {
        to[i] = from[i];
    }
    return destination;
}

void *memmove(void *destination, const void *source, size_t size)
{
    uint8_t *to = destination;
    const uint8_t *from = source;

    if (to < from) {
        for (size_t i = 0U; i < size; i++) {
            to[i] = from[i];
        }
    } else {
        for (size_t i = size; i > 0U; i--) {
            to[i - 1U] = from[i - 1U];
        }
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

int memcmp(const void *left, const void *right, size_t size)
{
    const uint8_t *a = left;
    const uint8_t *b = right;

    for (size_t i = 0U; i < size; i++) {
        if (a[i] != b[i]) {
            return (a[i] < b[i]) ? -1 : 1;
        }
    }
    return 0;
}

size_t strlen(const char *text)
{
    size_t length = 0U;

    while (text[length] != '\0') {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right)
{
    size_t i = 0U;

    while ((left[i] != '\0') && (left[i] == right[i])) {
        i++;
    }
    return (int)(uint8_t)left[i] - (int)(uint8_t)right[i];
}

int strncmp(const char *left, const char *right, size_t size)
{
    for (size_t i = 0U; i < size; i++) {
        if ((left[i] != right[i]) || (left[i] == '\0')) {
            return (int)(uint8_t)left[i] - (int)(uint8_t)right[i];
        }
    }
    return 0;
}
