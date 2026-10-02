# RTR-OS — Real-time Raspberry Operating System

Sistema operacional de tempo real para o **Raspberry Pi 4 Model B**, escrito em C, voltado a controle e automação.

O RTR-OS parte de duas ideias:

- **O sistema é síncrono.** Todos os núcleos leem o mesmo contador de hardware, movido pelo cristal da placa. Os instantes são sempre absolutos, de modo que um atraso em um ciclo nunca se acumula no seguinte.
- **O núcleo de processador é a unidade de garantia.** Um processo pode receber um núcleo inteiro, sem escalonador, sem tick e sem nenhuma interrupção, e rodar nele em tempo real. Os demais núcleos trabalham em concorrência, por prioridade fixa com limite de tempo imposto pelo kernel.

O projeto está no início. O que existe hoje é a base do kernel; a seção [Estado atual](#estado-atual) diz exatamente o que funciona e o que ainda não foi verificado.

## Sumário

- [Arquitetura](#arquitetura)
- [Estado atual](#estado-atual)
- [Estrutura do repositório](#estrutura-do-repositório)
- [Ferramentas necessárias](#ferramentas-necessárias)
- [Compilar](#compilar)
- [Testar no emulador](#testar-no-emulador)
- [Rodar na placa](#rodar-na-placa)
- [Regras de codificação](#regras-de-codificação)
- [Roteiro](#roteiro)
- [Documentação](#documentação)
- [Licença](#licença)

## Arquitetura

O desenho completo, com o motivo de cada escolha, está em [`docs/PLANO.md`](docs/PLANO.md). Em resumo:

| Tema | Decisão |
|------|---------|
| Núcleos | O núcleo 0 é do kernel e do sistema. Cada um dos núcleos 1 a 3 pode ser **dedicado** a um processo ou **compartilhado** entre tarefas. A alocação é fixada no boot |
| Núcleo dedicado | O processo roda isolado (EL0), em um núcleo que não recebe nenhuma interrupção em operação normal. Ele lê o contador e acessa os próprios dispositivos direto, sem entrar no kernel. Outro núcleo o supervisiona por batimento |
| Núcleos compartilhados | Prioridade fixa com preempção. Toda tarefa tem um limite de tempo de CPU por período; quem estoura é suspenso até o período seguinte. Se a soma dos limites passa de 100%, o sistema faz um rateio |
| Isolamento | Kernel em EL1, processos em EL0, cada um com o próprio espaço de endereçamento. As permissões são uma tabela fixa, aplicada no boot |
| Drivers | Ficam nos processos. No kernel, apenas o timer, o controlador de interrupções e um console de depuração |
| Comunicação | Canais de memória compartilhada sem trava, criados pelo kernel. O processo de tempo real nunca espera |
| Programas | Arquivos no cartão SD, carregados pelo sistema conforme um manifesto lido no boot |
| Observabilidade | O kernel oferece uma interface para qualquer processo empurrar dados e outra para lê-los, além de uma interface de parâmetros no sentido inverso |
| Interface web | Servida pela própria placa, por Ethernet, por processos de núcleo compartilhado. É por ela que o sistema é operado e monitorado |

## Estado atual

Etapas 0 e 1 do plano, implementadas e verificadas **no emulador**. Nenhuma delas foi verificada na placa física ainda.

O kernel hoje:

- dá boot em EL1 e inicializa o console serial (UART0, 115200 8N1);
- liga a MMU e as caches, com permissões por seção: código só para leitura e execução, constantes só para leitura, dados sem execução, periféricos sem cache;
- inicializa o controlador de interrupções (GIC-400) e o timer, que interrompe 1000 vezes por segundo em instantes absolutos;
- mede, pelo contador do sistema, o atraso entre o instante programado e a entrada do tratador, e relata no console a cada segundo;
- para com um relato no console diante de qualquer exceção inesperada.

Saída típica no console:

```text
RTR-OS - Real-time Raspberry Operating System
CPU 0 em EL1, contador a 54000000 Hz, dtb em 0x...
MMU e caches ligados
timer a 1000 interrupcoes por segundo
tempo 1 s | disparos 1000 | atraso em ns: min ..., med ..., max ..., pior ... | pulados 0 | irqs sem tratador 0 | console perdeu 0
```

No emulador, a frequência do contador aparece como 62500000 Hz; na placa, o esperado é 54000000 Hz.

O que **não** existe ainda: processos, escalonador, múltiplos núcleos, rede, interface web, leitura do cartão SD. Veja o [Roteiro](#roteiro).

## Estrutura do repositório

```text
kernel/
  boot/       entrada em assembly, vetores de exceção, script do linker
  board/      dispositivos da placa usados pelo kernel: UART, GIC
  core/       console, MMU, timer, tratamento de interrupções, parada por falha
  include/    cabeçalhos; arch.h concentra o acesso ao processador
cmake/        configuração da compilação cruzada
scripts/      compilar, testar no emulador, montar o cartão SD
sdcard/       config.txt do firmware da placa
docs/         plano do projeto e requisitos
```

## Ferramentas necessárias

O desenvolvimento é feito no Windows, com PowerShell 7.

| Ferramenta | Uso | Instalação |
|------------|-----|------------|
| LLVM (clang, lld, clang-tidy) | Compilação cruzada para AArch64 e análise estática | `winget install LLVM.LLVM` |
| CMake e Ninja | Sistema de build | `winget install Kitware.CMake Ninja-build.Ninja` |
| QEMU 9.0 ou mais novo | Emulação do Raspberry Pi 4 (`raspi4b`) | `winget install SoftwareFreedomConservancy.QEMU` |

Os scripts procuram o LLVM em `C:\Program Files\LLVM\bin` e o QEMU em `C:\Program Files\qemu` quando eles não estão no `PATH`.

Para rodar na placa:

- Raspberry Pi 4 Model B e fonte;
- cartão microSD formatado em FAT32;
- adaptador USB-serial de **3,3 V** (um adaptador de 5 V pode danificar a placa).

## Compilar

```powershell
scripts\build.ps1
```

Gera `build\kernel8.img`, a imagem que o firmware da placa carrega, e `build\kernel8.elf`, com símbolos para depuração. A compilação falha diante de qualquer aviso do compilador ou do analisador estático.

## Testar no emulador

Execução sem interação, com a saída do console mostrada ao final:

```powershell
scripts\test-qemu.ps1                 # 6 segundos, relógio do Windows
scripts\test-qemu.ps1 -Virtual        # relógio virtual determinístico
scripts\test-qemu.ps1 -Seconds 15
```

Com o relógio do Windows, o emulador entrega as interrupções do timer com 1 a 2 ms de atraso e pula períodos; isso é efeito do agendador do Windows, não do kernel. Com `-Virtual`, o tempo avança pela contagem de instruções e o atraso medido é constante.

Execução interativa, com o console no terminal (para sair: `Ctrl+A` e depois `X`):

```powershell
scripts\run-qemu.ps1
scripts\run-qemu.ps1 -Gdb             # espera um depurador em localhost:1234
```

Ensaio de proteção de memória, que compila uma versão do kernel que tenta gravar no próprio código e confere se a MMU barra a escrita:

```powershell
scripts\test-fault.ps1
```

Limites do emulador:

- ele valida a lógica, não o tempo; nenhuma medida de latência feita nele vale para a placa;
- o QEMU não emula o controlador Ethernet do Pi 4, então rede e interface web só podem ser testadas na placa.

## Rodar na placa

1. Monte o conteúdo do cartão. O script baixa, uma única vez, os arquivos de firmware do repositório oficial da Raspberry Pi e junta a eles `config.txt` e `kernel8.img`:

   ```powershell
   scripts\make-sdcard.ps1               # só monta em build\sdcard
   scripts\make-sdcard.ps1 -Drive E:     # monta e copia para o cartão em E:
   ```

   Com `-Drive`, o script recusa unidades que não sejam removíveis ou não estejam em FAT32.

2. Ligue o adaptador USB-serial ao conector GPIO da placa:

   | Adaptador | Pino da placa |
   |-----------|---------------|
   | GND | 6 (GND) |
   | RX | 8 (GPIO14, TXD) |
   | TX | 10 (GPIO15, RXD) |

   Não ligue o fio de 5 V do adaptador.

3. Abra um terminal serial a 115200 baud, 8 bits, sem paridade, 1 bit de parada.

4. Coloque o cartão na placa e energize.

## Regras de codificação

Obrigatórias no kernel, verificadas a cada compilação:

- sem recursão;
- todo laço com limite fixo;
- sem alocação dinâmica de memória depois da partida;
- tipos de tamanho fixo e nenhum comportamento indefinido da linguagem;
- todo valor de retorno conferido; avisos tratados como erro.

Há dois laços sem fim permitidos no kernel: o laço principal e a parada do sistema. Assembly embutido e conversão de inteiro em ponteiro ficam restritos a `kernel/include/arch.h`.

A lista de verificações do analisador está em [`.clang-tidy`](.clang-tidy).

## Roteiro

A primeira meta é o sistema no ar e acessível pelo navegador; os núcleos dedicados vêm em seguida.

| Etapa | Entrega | Situação |
|-------|---------|----------|
| 0 | Bancada: toolchain, emulador, cartão SD, console serial | Verificada no emulador |
| 1 | Base: MMU e caches, exceções, GIC, timer | Verificada no emulador |
| 2 | Processos isolados e prioridade fixa com limite de tempo | A fazer |
| 3 | Dispositivos em processo e tabela de permissões | A fazer |
| 4 | Rede: driver da Ethernet e pilha lwIP | A fazer |
| 5 | Serviço web e página de estatísticas | A fazer |
| 6 | Observabilidade e parâmetros | A fazer |
| 7 | Multicore | A fazer |
| 8 | Piso do hardware e núcleo dedicado | A fazer |
| 9 | Canais entre núcleos | A fazer |
| 10 | Carga de programas do cartão SD e manifesto | A fazer |
| 11 | Entrada e saída de automação: GPIO, serial, SPI, I2C | A fazer |

Os critérios de aceite de cada etapa estão no plano.

## Documentação

- [`docs/PLANO.md`](docs/PLANO.md) — decisões, requisitos, arquitetura, etapas e riscos.
- [`docs/ESTATISTICAS.md`](docs/ESTATISTICAS.md) — requisitos da página de estatísticas do sistema (proposta em revisão).

## Licença

Código fechado. Todos os direitos reservados.

A pilha de rede prevista (lwIP) é de terceiros e distribuída sob licença BSD.
