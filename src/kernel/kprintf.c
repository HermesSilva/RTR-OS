/* RTR-OS - saída formatada no console e rotinas de memória exigidas pelo compilador. */
#include <stdbool.h>

#include "arch.h"
#include "drivers.h"
#include "kernel.h"

static void put_padding(int count, char pad)
{
    while (count-- > 0)
        uart_putc(pad);
}

static void put_string(const char *s, int width, bool left)
{
    int len = 0;

    if (!s)
        s = "(null)";
    while (s[len])
        len++;

    if (!left)
        put_padding(width - len, ' ');
    for (int i = 0; i < len; i++)
        uart_putc(s[i]);
    if (left)
        put_padding(width - len, ' ');
}

static void put_number(uint64_t value, unsigned base, bool negative, int width, bool left, char pad)
{
    char digits[24];
    int len = 0;

    do {
        digits[len++] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value);

    width -= len + (negative ? 1 : 0);

    if (negative && pad == '0')
        uart_putc('-');
    if (!left)
        put_padding(width, pad);
    if (negative && pad != '0')
        uart_putc('-');
    while (len > 0)
        uart_putc(digits[--len]);
    if (left)
        put_padding(width, ' ');
}

/* Aceita %c %s %d %u %x %p %%, com largura, '-' e '0', e os modificadores l e ll. */
void kvprintf(const char *fmt, va_list ap)
{
    for (; *fmt; fmt++) {
        bool left = false;
        char pad = ' ';
        int width = 0;
        int longs = 0;

        if (*fmt != '%') {
            uart_putc(*fmt);
            continue;
        }
        fmt++;

        for (;; fmt++) {
            if (*fmt == '-')
                left = true;
            else if (*fmt == '0')
                pad = '0';
            else
                break;
        }
        if (left)
            pad = ' ';
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        while (*fmt == 'l') {
            longs++;
            fmt++;
        }

        switch (*fmt) {
        case 'c':
            uart_putc((char)va_arg(ap, int));
            break;
        case 's':
            put_string(va_arg(ap, const char *), width, left);
            break;
        case 'd': {
            int64_t v = longs ? va_arg(ap, long) : va_arg(ap, int);
            put_number(v < 0 ? -(uint64_t)v : (uint64_t)v, 10, v < 0, width, left, pad);
            break;
        }
        case 'u':
            put_number(longs ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int),
                       10, false, width, left, pad);
            break;
        case 'x':
            put_number(longs ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int),
                       16, false, width, left, pad);
            break;
        case 'p':
            uart_putc('0');
            uart_putc('x');
            put_number((uintptr_t)va_arg(ap, void *), 16, false, 0, false, ' ');
            break;
        case '%':
            uart_putc('%');
            break;
        case '\0':
            return;
        default:
            uart_putc('%');
            uart_putc(*fmt);
            break;
        }
    }
}

void kprintf(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
}

void panic(const char *fmt, ...)
{
    va_list ap;

    irq_save();
    kprintf("\n*** PANIC: ");
    va_start(ap, fmt);
    kvprintf(fmt, ap);
    va_end(ap);
    kprintf("\n");

    for (;;)
        __asm__ volatile("wfe");
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;

    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;

    while (n--)
        *d++ = *s++;
    return dst;
}
