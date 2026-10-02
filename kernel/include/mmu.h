/* RTR-OS - tradução de endereços do kernel. */
#ifndef RTR_MMU_H
#define RTR_MMU_H

/*
 * Monta o mapa do kernel e liga MMU e caches. Os endereços não mudam (mapa
 * identidade); o que passa a valer são as permissões:
 *
 *   código do kernel         leitura e execução
 *   constantes               só leitura
 *   dados, pilha e RAM       leitura e escrita, sem execução
 *   periféricos              leitura e escrita, sem execução, sem cache
 *
 * Cobre o primeiro gigabyte de RAM e a janela dos periféricos; o resto do
 * espaço de endereços fica sem mapeamento e qualquer acesso a ele é falha.
 */
void mmu_init(void);

#ifdef RTR_FAULT_TEST
/* Só existe na compilação de ensaio (opção RTR_FAULT_TEST do CMake). */
void mmu_fault_test(void);
#endif

#endif
