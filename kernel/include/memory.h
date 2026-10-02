/*
 * RTR-OS - rotinas de memória.
 *
 * O compilador gera chamadas a memcpy e memset por conta própria (cópia de
 * estruturas, inicialização), por isso elas precisam existir com esses nomes.
 */
#ifndef RTR_MEMORY_H
#define RTR_MEMORY_H

#include <stddef.h>

void *memcpy(void *destination, const void *source, size_t size);
void *memset(void *destination, int value, size_t size);

#endif
