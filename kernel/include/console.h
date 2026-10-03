/* RTR-OS - kernel debug console. */
#ifndef RTR_CONSOLE_H
#define RTR_CONSOLE_H

#include <stdint.h>

/* Longer texts are cut: no console loop is unbounded. */
#define CONSOLE_MAX_TEXT 256U

void console_init(void);
void console_put_char(char c);
void console_put_text(const char *text);
void console_put_dec(uint64_t value);
void console_put_hex(uint64_t value);

/* Characters lost because the transmitter did not accept them in time. */
uint32_t console_dropped(void);

#endif
