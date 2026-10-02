# RTR-OS — Plano

Situação: **em revisão**, decisão por decisão. Valem como aprovadas apenas as linhas da seção 1. Código novo só depois da aprovação do plano.

## 1. Decisões já tomadas

| Tema | Decisão |
|------|---------|
| Nome | RTR-OS — Real-time Raspberry Operating System |
| Placa alvo | Raspberry Pi 4 Model B (BCM2711, 4 × Cortex-A72, GIC-400) |
| Linguagem | C, com assembly só onde for inevitável |
| Finalidade | Controle e automação |
| Testes | Na placa física, com emulador (QEMU `raspi4b`) como apoio |
| Inovação | Garantias de tempo, isolamento e segurança, criticidade mista multicore, observabilidade |
| Requisito central | Um processo pode receber um núcleo de forma exclusiva e rodar nele em tempo real, enquanto os outros núcleos trabalham em concorrência |
| Modelo de execução | Síncrono com o relógio: os processos avançam como engrenagens na mesma base de tempo; perder o passo é falha, não atraso tolerável |
| Relógio mestre | O cristal do próprio Pi; não há referência externa |
| Escopo | Sistema de uso geral em automação; o conjunto a atender vai muito além de um único tipo de aplicação |
| Caso de ensaio | Modulador PWM por software no núcleo dedicado, temporizado pelo cristal, operando no limite do hardware. É um exemplo da classe de trabalho de tempo real e serve para medir o núcleo dedicado; não é a aplicação do sistema |
| D2 — privilégio do processo dedicado | Processo isolado em EL0, em um núcleo sem nenhuma interrupção em operação normal, supervisionado por outro núcleo (detalhes na seção 4) |
| D3 — alocação de núcleos | Fixa no boot. O modo de cada núcleo não muda com o sistema rodando; flexibilidade em execução foi descartada por onerar o projeto sem ganho real |
| D3a — descrição da instalação | Arquivo de manifesto no cartão, lido uma única vez no boot: modo de cada núcleo, programas a carregar, núcleo e contrato de cada um. Uma só imagem de kernel serve a todas as instalações |
| D4 — quantidade de núcleos dedicados | O núcleo 0 é do kernel e do sistema. Cada um dos núcleos 1 a 3 pode ser dedicado ou compartilhado, conforme a necessidade de cada instalação |
| D5 — local dos drivers | Nos processos. No kernel ficam só o timer, o controlador de interrupções e um console mínimo de depuração. Dispositivo exclusivo de um processo é acessado direto por ele; dispositivo compartilhado só é acessado por intermédio do kernel |
| D5a — acesso a dispositivo compartilhado | Canal criado pelo kernel: o processo pede o acesso uma vez, na partida; o kernel confere a permissão e cria uma fila sem trava em memória compartilhada com o driver. Durante o laço de tempo real não há entrada no kernel |
| D7 — interfaces de E/S | Pinos de entrada e saída (GPIO), serial (UART) e SPI, nessa ordem. I2C e Ethernet ficam fora por enquanto; USB fica fora do plano |
| D8 — interface dos programas | Própria do RTR-OS e pequena, expressando contratos, ciclos, canais e capacidades, sem chamadas que bloqueiam. Os programas contam também com a parte da biblioteca padrão de C que não depende do sistema. POSIX fica fora do plano |
| D9 — norma de codificação | Conjunto próprio e curto de regras, verificado por ferramenta a cada compilação (detalhes na seção 4). Obrigatório no kernel e no conjunto de partida; recomendado nos programas |
| D10 — organização | Repositório git com o remoto `github.com/HermesSilva/RTR-OS`; o envio ao remoto é feito só por ordem expressa. Código fechado, todos os direitos reservados, até decisão em contrário. A pasta `RTR-SO` será renomeada para `RTR-OS` ao fim de uma sessão de trabalho |
| D6 — origem dos programas | Carregados do cartão SD pelo próprio sistema. Cada processo é um arquivo binário no cartão |
| D6a — partida | Conjunto de partida embutido: o driver do cartão, o sistema de arquivos e o carregador são processos que vão dentro do `kernel8.img`. Todo o resto vem do cartão |

## 2. Visão

Um sistema operacional para controle e automação em que o tempo é um recurso garantido pelo kernel, e não uma consequência de prioridades bem ajustadas.

Duas ideias o distinguem:

- **O sistema é síncrono.** Todos os núcleos leem o mesmo contador de hardware, movido pelo cristal. Os instantes são sempre absolutos (início + n × período), de modo que um atraso em um ciclo nunca se acumula no seguinte.
- **O núcleo de processador é a unidade de garantia.** Cada núcleo opera em um de dois modos:
  - **Dedicado**: pertence a um único processo. Não há escalonador, tick nem interrupção nesse núcleo; o processo é dono do tempo dele.
  - **Compartilhado**: várias tarefas dividem o núcleo sob contrato (período, orçamento, deadline). O kernel só aceita um contrato que consegue cumprir e impede que uma tarefa gaste mais do que declarou.

## 3. Requisitos

### Funcionais

- **RF1** — Alocar um núcleo com exclusividade a um processo.
- **RF2** — Rodar tarefas concorrentes nos demais núcleos, com contrato temporal por tarefa.
- **RF3** — Recusar, na criação, a tarefa cujo contrato não possa ser cumprido.
- **RF4** — Conter a tarefa de núcleo compartilhado que estoura o orçamento, sem afetar as outras.
- **RF5** — Isolar processos entre si e do kernel; a falha de um não derruba o sistema.
- **RF6** — Trocar dados entre o núcleo dedicado e os compartilhados sem bloquear o lado de tempo real.
- **RF7** — Medir e expor latência, jitter, uso de orçamento e deadlines perdidos, por núcleo e por tarefa.
- **RF8** — Acessar a E/S de automação por pinos de entrada e saída, serial e SPI (D7). Analógico e CAN dependem de chips externos em SPI.
- **RF9** — Detectar que o processo dedicado perdeu o passo e levar as saídas dele a um estado seguro.
- **RF10** — Dar ao processo dedicado acesso direto ao contador de tempo e aos registradores dos seus dispositivos, sem chamada ao kernel.

### Tempo real

- **RT1** — Em operação normal, nenhuma interrupção chega ao núcleo dedicado, nem as do kernel.
- **RT2** — O passo de tempo e a variação de cada borda gerada pelo processo dedicado são os menores que o hardware permite. Não há meta numérica: o valor é o que a medição na placa mostrar, e o trabalho é aproximá-lo do limite físico.
- **RT3** — Toda operação do kernel tem tempo limitado por constantes de configuração, nunca pela carga.
- **RT4** — Nos núcleos compartilhados, nenhuma tarefa dentro do orçamento perde deadline.
- **RT5** — A frequência média de qualquer sinal gerado é exatamente a derivada do cristal, sem deriva acumulada.

### Limite conhecido do hardware

O contador do sistema avança a 54 MHz, em passos de 18,5 ns. No PWM por software, frequência e resolução dividem esse orçamento:

| Frequência do PWM | Passos por período | Resolução |
|---|---|---|
| 1 kHz | 54.000 | cerca de 15,7 bits |
| 20 kHz | 2.700 | cerca de 11,4 bits |
| 100 kHz | 540 | cerca de 9 bits |
| 1 MHz | 54 | cerca de 5,8 bits |
| 5,4 MHz | 10 | cerca de 3,3 bits |

Contar ciclos da CPU (1,5 GHz, também derivados do cristal) pode reduzir o passo para menos de 1 ns. Isso não foi verificado e depende de a frequência da CPU permanecer fixa.

## 4. Arquitetura

### Núcleos

- **Decidido (D3)**: o modo de cada núcleo (dedicado ou compartilhado) é fixado no boot e vale até o desligamento. Não há migração de tarefas nem troca de modo em execução. Reiniciar o processo de um núcleo dedicado não exige reiniciar a placa.
- **Decidido (D3a)**: a instalação é descrita por um manifesto no cartão, lido pelo carregador no boot. Os núcleos 1 a 3 ficam parados até a leitura. O kernel valida cada pedido (admissão dos contratos, permissões) e, terminada a partida, a configuração é selada até o desligamento. Manifesto ausente ou inválido: o sistema não parte e relata o erro no console.
- **Núcleo 0** é sempre compartilhado: faz o boot, hospeda os serviços do sistema e supervisiona os núcleos dedicados (consequência da D2).
- **Decidido (D4)**: cada um dos **núcleos 1 a 3** pode ser dedicado ou compartilhado, em qualquer combinação, de zero a três dedicados.
- A validação começa com um núcleo dedicado. Configurações com dois e três só são dadas como garantidas depois de medida a interferência entre eles na placa.
- Cada núcleo tem estado próprio (escalonador, filas, contadores). O caminho crítico de um núcleo não toma trava que outro núcleo possa segurar.

### Núcleo dedicado (decidido — D2)

- O processo roda **isolado em EL0**, com espaço de endereçamento próprio.
- O contador de tempo é legível pelo processo, e os registradores dos seus dispositivos são mapeados direto no espaço dele. O laço de tempo real roda sem nenhuma chamada ao kernel.
- A temporização fina é por **espera ativa** sobre o contador; o processo ocupa 100% do núcleo.
- **Nenhuma interrupção** é roteada para esse núcleo em operação normal.
- **Supervisão por outro núcleo**: o processo atualiza um contador de batimento em memória compartilhada, e o núcleo 0 confere se ele avança.
- **Interrupção só para parar**: se o batimento falhar, o núcleo 0 interrompe o núcleo dedicado e o kernel leva as saídas a um estado seguro. Em EL0 o processo não consegue bloquear essa interrupção.
- O que não é de tempo real (registro, configuração, rede) é pedido a outro núcleo por um canal sem bloqueio.

### Núcleos compartilhados (proposta)

- Escalonamento EDF por núcleo, com orçamento imposto e teste de admissão.
- Períodos múltiplos de um ciclo base comum, em instantes absolutos, para que as tarefas fiquem em fase com o resto do sistema.
- Timer em modo one-shot: o núcleo só é interrompido no próximo evento que importa.
- O tempo que sobra dos contratos vai para tarefas sem garantia (melhor esforço).

### Isolamento (proposta)

- Kernel em EL1, processos em EL0, cada um com seu espaço de endereçamento (MMU).
- Acesso a recursos por **capacidades**: região de memória, dispositivo, interrupção, canal, núcleo e orçamento de tempo são objetos que o processo recebe explicitamente.
- Kernel pequeno: tempo, memória, capacidades e comunicação.
- **Decidido (D5)**: os drivers de E/S ficam nos processos. O kernel mantém apenas o timer, o controlador de interrupções e um console mínimo de depuração.
- **Decidido (D5)**: um dispositivo exclusivo é mapeado no processo dono, que o acessa direto. Um dispositivo compartilhado nunca é mapeado no processo de tempo real: ele fala com o kernel, que é a autoridade sobre o acesso.
- **Decidido (D5a)**: essa conversa acontece na partida do processo, antes do laço de tempo real. O kernel confere a permissão e cria um canal com o driver; pode revogá-lo depois. Chamadas diretas ao kernel ficam para configuração, fora do laço.

### Comunicação entre núcleos

- **Decidido (D5a)**: canais de memória compartilhada, sem trava, criados e controlados pelo kernel.
- Quem está no núcleo dedicado nunca espera pelo outro lado; a resposta de um pedido chega em um ciclo posterior, pela fila de volta.
- Proposta: um produtor e um consumidor por canal. A regra para fila cheia (descartar e contar, por exemplo) ainda precisa ser definida.

### Observabilidade (proposta)

- Buffer de rastreamento por núcleo, escrito sem trava.
- Contadores por tarefa e por núcleo, lidos por um serviço fora do caminho de tempo real.
- Saída pela serial. Rede não está no plano por enquanto (D7).

### Regras de codificação (decidido — D9)

- Sem recursão, para a pilha ter tamanho máximo conhecido.
- Todo laço com limite fixo, para o tempo de execução ser limitado (RT3).
- Sem alocação dinâmica de memória depois da partida.
- Tipos de tamanho fixo e nenhum comportamento indefinido da linguagem.
- Todo valor de retorno conferido; avisos do compilador tratados como erro.
- Verificação automática a cada compilação, com o compilador e os analisadores estáticos do LLVM.
- A lista é inspirada na MISRA C e nas regras da NASA para código crítico, sem pretensão de certificação. As exceções inevitáveis em código de kernel (endereços de registradores, assembly) ficam restritas a arquivos identificados.

## 5. Etapas

Cada etapa termina com algo medido na placa. A ordem reflete a D2: o processo isolado vem antes do núcleo dedicado.

| Etapa | Entrega | Critério de aceite |
|-------|---------|--------------------|
| 0. Bancada | Toolchain, emulador, gravação do cartão SD, console serial | Mensagem de boot lida na serial da placa física |
| 1. Base e piso do hardware | MMU e caches, exceções, GIC, timer; ensaio de troca de pino em laço no nível do kernel | Passo de tempo, taxa máxima de troca do pino e variação de borda medidos com osciloscópio e registrados |
| 2. Multicore | Partida dos 4 núcleos, estado por núcleo, interrupção entre núcleos | Os 4 núcleos rodando código próprio, cada um reportando na serial |
| 3. Processo isolado | Processo em EL0 com espaço de endereçamento próprio, contador legível e dispositivo mapeado | Processo que acessa memória alheia é encerrado e o resto segue |
| 4. Núcleo dedicado | Núcleo sem interrupções entregue a um processo; PWM por software; supervisão por batimento | Variação de borda igual à do piso da etapa 1, com os outros 3 núcleos sob carga máxima; processo travado é detectado e a saída vai ao estado seguro |
| 5. Contratos | EDF com orçamento e admissão nos núcleos compartilhados | Tarefa defeituosa contida sem nenhuma perda de deadline nas demais |
| 6. Capacidades | Recursos concedidos e revogados como objetos explícitos | Processo sem a capacidade de um dispositivo não consegue acessá-lo |
| 7. Canais | Comunicação sem trava entre núcleo dedicado e compartilhados | Troca de dados sem alterar a variação de borda medida na etapa 4 |
| 8. Carga do cartão | Driver do cartão SD, leitura de FAT, carregador de programas e leitura do manifesto (D6, D3a) | Processo gravado como arquivo no cartão é carregado e executa; cartão ausente ou arquivo corrompido resulta em falha relatada, sem travar o sistema |
| 9. E/S | Drivers de GPIO, serial e SPI em espaço de processo (D7) | Malha de controle real fechada pelo núcleo dedicado, com leitura por SPI e comando pela serial |
| 10. Rastreamento | Coleta por núcleo e ferramenta de leitura no PC | Linha do tempo de um ensaio reconstruída no PC |

Até a etapa 8, os programas das etapas 3 a 7 entram na memória embutidos na imagem do kernel. O mesmo mecanismo fica em definitivo para o conjunto de partida (D6a).

## 6. Decisões em aberto

Discutidas uma por vez. As decisões D2 a D10 foram fechadas e estão na seção 1. Resta a D1, que depende de medição, e a confirmação dos itens da seção 4 ainda marcados como proposta.

| # | Decisão | Recomendação |
|---|---------|--------------|
| D1 | Metas numéricas para os núcleos compartilhados (latência, jitter) | Fixar depois da medição da etapa 1 |

## 7. Bancada necessária

- Adaptador USB-serial de 3,3 V, para o console na placa (pinos 6, 8 e 10 do conector GPIO). Sem ele não há saída visível na placa, pois ainda não existe driver de vídeo.
- Cartão microSD e leitor.
- Osciloscópio, com banda suficiente para ver bordas de dezenas de nanossegundos. É obrigatório: RT2 e os critérios das etapas 1 e 4 só se verificam medindo o pino por fora.

## 8. Riscos

- **Interferência entre núcleos pelo hardware**: os 4 núcleos dividem a cache L2 e o barramento de memória e de periféricos. Um núcleo compartilhado sob carga pode deslocar bordas do dedicado mesmo sem nenhuma interferência do kernel. É o maior risco técnico para RT2 e só aparece na placa.
- **Firmware da GPU**: o firmware da Broadcom continua ativo depois do boot e controla clocks e temperatura. Se ele alterar a frequência da CPU, a contagem de ciclos deixa de servir como base de tempo. O efeito sobre o determinismo é desconhecido.
- **Caminho até o pino**: a escrita no registrador de GPIO atravessa um barramento cuja latência e variação não são conhecidas. Elas definem o piso real da variação de borda.
- **O emulador não reproduz tempo**: o QEMU valida lógica, não latência. Toda afirmação de tempo real precisa de medição na placa.
- **Periféricos difíceis**: no Pi 4, o USB fica atrás de um controlador PCIe e o Ethernet exige driver próprio. Convém evitá-los nas primeiras etapas.
- **Cartão SD no caminho do boot (D6)**: o sistema passa a depender de um driver de cartão, que é trabalhoso e varia com o cartão usado, e a leitura vira um ponto de falha na partida. Leituras durante a operação usam o barramento compartilhado e podem interferir nos núcleos dedicados; o efeito precisa ser medido.

## 9. Estado atual

Existe um **rascunho exploratório**, escrito antes deste plano. Ele não fixa nenhuma decisão de arquitetura e pode ser descartado.

- Contém: boot em EL1, console serial, GIC, timer one-shot, troca de contexto e EDF com orçamento e admissão, em um único núcleo e sem MMU.
- Verificado no QEMU 11.1 (`raspi4b`): quatro tarefas periódicas sem perda de deadline, uma tarefa em laço infinito contida em todos os períodos e um quinto contrato recusado por excesso de carga.
- **Não verificado**: execução na placa física, `scripts/run-qemu.ps1` e `scripts/make-sdcard.ps1`.
- Ferramentas instaladas no Windows: LLVM/clang 23.1.2 e QEMU 11.1.0.
