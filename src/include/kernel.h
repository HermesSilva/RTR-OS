/* RTR-OS - serviços internos do kernel. */
#ifndef RTR_KERNEL_H
#define RTR_KERNEL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

void kprintf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void kvprintf(const char *fmt, va_list ap);
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);

#endif
