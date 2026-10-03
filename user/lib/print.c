/* RTR-OS - program text output on the debug console. */
#include <rtr/user.h>

#define DEC_DIGITS_MAX  20U
#define HEX_DIGITS      16U

void rtr_print(const char *text)
{
    size_t length = 0U;

    if (text == NULL) {
        return;
    }
    while ((length < RTR_CONSOLE_WRITE_MAX) && (text[length] != '\0')) {
        length++;
    }
    if (length != 0U) {
        (void)rtr_syscall(RTR_SYS_CONSOLE_WRITE, (uint64_t)(uintptr_t)text, length);
    }
}

void rtr_print_dec(uint64_t value)
{
    char digits[DEC_DIGITS_MAX + 1U];
    size_t position = DEC_DIGITS_MAX;

    digits[position] = '\0';
    do {
        position--;
        digits[position] = (char)('0' + (char)(value % 10U));
        value /= 10U;
    } while ((value != 0U) && (position > 0U));

    rtr_print(&digits[position]);
}

void rtr_print_hex(uint64_t value)
{
    static const char table[] = "0123456789abcdef";
    char digits[HEX_DIGITS + 3U];

    digits[0] = '0';
    digits[1] = 'x';
    for (size_t i = 0U; i < HEX_DIGITS; i++) {
        digits[i + 2U] = table[(value >> ((HEX_DIGITS - 1U - i) * 4U)) & 0xFU];
    }
    digits[HEX_DIGITS + 2U] = '\0';
    rtr_print(digits);
}
