/* RTR-OS - kernel debug console, over UART0. */
#include "console.h"

#include <stddef.h>

#include "board.h"

#define DEC_DIGITS_MAX  20U             /* 2^64 - 1 has 20 decimal digits */
#define HEX_DIGITS      16U

static uint32_t dropped;

static void put_byte(uint8_t byte)
{
    if (!uart_send(byte)) {
        dropped++;
    }
}

void console_init(void)
{
    uart_init();
}

void console_put_char(char c)
{
    if (c == '\n') {
        put_byte((uint8_t)'\r');
    }
    put_byte((uint8_t)c);
}

void console_put_text(const char *text)
{
    if (text == NULL) {
        return;
    }
    for (uint32_t i = 0U; (i < CONSOLE_MAX_TEXT) && (text[i] != '\0'); i++) {
        console_put_char(text[i]);
    }
}

void console_put_dec(uint64_t value)
{
    char digits[DEC_DIGITS_MAX];
    uint32_t count = 0U;

    do {
        digits[count] = (char)('0' + (char)(value % 10U));
        count++;
        value /= 10U;
    } while ((value != 0U) && (count < DEC_DIGITS_MAX));

    while (count > 0U) {
        count--;
        console_put_char(digits[count]);
    }
}

void console_put_hex(uint64_t value)
{
    static const char table[] = "0123456789abcdef";

    console_put_text("0x");
    for (uint32_t i = 0U; i < HEX_DIGITS; i++) {
        uint32_t shift = (HEX_DIGITS - 1U - i) * 4U;

        console_put_char(table[(value >> shift) & 0xFU]);
    }
}

uint32_t console_dropped(void)
{
    return dropped;
}
