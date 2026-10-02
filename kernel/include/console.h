/* RTR-OS - console de depuração do kernel. */
#ifndef RTR_CONSOLE_H
#define RTR_CONSOLE_H

#include <stdint.h>

/* Textos maiores que isto são cortados: nenhum laço do console é ilimitado. */
#define CONSOLE_MAX_TEXT 256U

void console_init(void);
void console_put_char(char c);
void console_put_text(const char *text);
void console_put_dec(uint64_t value);
void console_put_hex(uint64_t value);

/* Caracteres perdidos porque o transmissor não os aceitou a tempo. */
uint32_t console_dropped(void);

#endif
