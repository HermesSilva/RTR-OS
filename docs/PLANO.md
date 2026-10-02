# RTR-OS — Plano

Situação: **em implementação**, etapa por etapa, desde 2026-10-02. As decisões da seção 1 estão aprovadas. Os itens ainda marcados como proposta e as decisões da seção 6 são fechados antes da etapa que depende deles.

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
| Interface web | O RTR-OS terá uma interface web, e é por ela que o sistema também será monitorado. A observabilidade precisa ser dinâmica e flexível |
| Primeira meta de implementação | Levantar o sistema e acessá-lo pelo navegador: o kernel rodando mais o serviço web. Os núcleos dedicados vêm depois |
| Modelo de execução | Síncrono com o relógio: os processos avançam como engrenagens na mesma base de tempo; perder o passo é falha, não atraso tolerável |
| Relógio mestre | O cristal do próprio Pi; não há referência externa |
| Escopo | Sistema de uso geral em automação; o conjunto a atender vai muito além de um único tipo de aplicação |
| Caso de ensaio | Modulador PWM por software no núcleo dedicado, temporizado pelo cristal, operando no limite do hardware. É um exemplo da classe de trabalho de tempo real e serve para medir o núcleo dedicado; não é a aplicação do sistema |
| D2 — privilégio do processo dedicado | Processo isolado em EL0, em um núcleo sem nenhuma interrupção em operação normal, supervisionado por outro núcleo (detalhes na seção 4) |
| D3 — alocação de núcleos | Fixa no boot. O modo de cada núcleo não muda com o sistema rodando; flexibilidade em execução foi descartada por onerar o projeto sem ganho real |
| D3a — descrição da instalação | Arquivo de manifesto no cartão, lido uma única vez no boot: modo de cada núcleo, programas a carregar e, para cada um, núcleo, prioridade, período e limite de tempo. Uma só imagem de kernel serve a todas as instalações |
| D4 — quantidade de núcleos dedicados | O núcleo 0 é do kernel e do sistema. Cada um dos núcleos 1 a 3 pode ser dedicado ou compartilhado, conforme a necessidade de cada instalação |
| D5 — local dos drivers | Nos processos. No kernel ficam só o timer, o controlador de interrupções e um console mínimo de depuração. Dispositivo exclusivo de um processo é acessado direto por ele; dispositivo compartilhado só é acessado por intermédio do kernel |
| D5a — acesso a dispositivo compartilhado | Canal criado pelo kernel: o processo pede o acesso uma vez, na partida; o kernel confere a permissão e cria uma fila sem trava em memória compartilhada com o driver. Durante o laço de tempo real não há entrada no kernel |
| D7 — interfaces de E/S | Para os processos de tempo real (núcleo dedicado): pinos de entrada e saída (GPIO), serial (UART) e SPI; I2C e Ethernet ficam fora por enquanto. Para os processos de núcleo compartilhado não há essa restrição: podem acessar tudo, inclusive I2C e Ethernet. USB segue fora do plano |
| D13 — observabilidade | O kernel oferece duas interfaces. Uma de escrita, igual para todos os processos, inclusive os de tempo real, por onde cada um empurra os seus dados. Outra de leitura, usada por quem precisa consultar esses dados e os contadores |
| D15 — parâmetros | A interface web também altera parâmetros dos processos em funcionamento, por uma interface de parâmetros do kernel. O processo declara os parâmetros na partida; quem tem permissão grava pelo kernel, que valida; o processo lê o valor atual direto da memória |
| D17 — caminho até a web | O serviço web já nasce como processo isolado; não há versão provisória dentro do kernel |
| D16 — pilha de rede | Pilha de terceiros adaptada: lwIP (licença BSD), rodando em um processo de núcleo compartilhado. O driver da Ethernet é escrito para o RTR-OS |
| D14 — interface web | Servida pelo próprio Pi, por Ethernet, por processos de núcleo compartilhado |
| D8 — interface dos programas | Própria do RTR-OS e pequena, expressando núcleo dedicado, prioridades e canais. O processo de tempo real nunca bloqueia. Os programas contam também com a parte da biblioteca padrão de C que não depende do sistema. POSIX fica fora do plano |
| D9 — norma de codificação | Conjunto próprio e curto de regras, verificado por ferramenta a cada compilação (detalhes na seção 4). Obrigatório no kernel e no conjunto de partida; recomendado nos programas |
| D10 — organização | Repositório git com o remoto `github.com/HermesSilva/RTR-OS`; esse remoto é público, então nada é enviado a ele por enquanto e o histórico fica só na máquina local. Código fechado, todos os direitos reservados, até decisão em contrário. A pasta `RTR-SO` será renomeada para `RTR-OS` ao fim de uma sessão de trabalho |
| D11 — núcleos compartilhados | Escalonamento por prioridade fixa com preempção: roda sempre a tarefa pronta de maior prioridade. As propostas de contratos com EDF e de tabela de tempo foram descartadas |
| D11a — limite de tempo | Toda tarefa de núcleo compartilhado tem um limite de tempo de CPU por período: o declarado no manifesto ou, na falta dele, um limite default. Não existe tarefa sem limite. Quem estoura é suspenso pelo kernel até o período seguinte |
| D11b — excesso de carga | Se a soma dos limites das tarefas de um núcleo passar de 100%, o sistema não recusa a partida: faz um rateio, reduzindo os limites na mesma proporção até a soma ficar abaixo de 100% |
| D11c — rateio e tarefas do sistema | O rateio atinge todas as tarefas, inclusive as do sistema. As tarefas do sistema têm um limite mínimo, abaixo do qual o rateio não as reduz, para garantir a supervisão |
| D12 — permissões | Tabela fixa do manifesto: ele declara os dispositivos, as regiões de memória e os canais de cada processo; o kernel aplica no boot e sela. Não há concessão, repasse nem revogação em execução |
| D6 — origem dos programas | Carregados do cartão SD pelo próprio sistema. Cada processo é um arquivo binário no cartão |
| D6a — partida | Conjunto de partida embutido: o driver do cartão, o sistema de arquivos e o carregador são processos que vão dentro do `kernel8.img`. Todo o resto vem do cartão |

## 2. Visão

Um sistema operacional para controle e automação em que o tempo é um recurso garantido pelo kernel, e não uma consequência de prioridades bem ajustadas.

Duas ideias o distinguem:

- **O sistema é síncrono.** Todos os núcleos leem o mesmo contador de hardware, movido pelo cristal. Os instantes são sempre absolutos (início + n × período), de modo que um atraso em um ciclo nunca se acumula no seguinte.
- **O núcleo de processador é a unidade de garantia.** Cada núcleo opera em um de dois modos:
  - **Dedicado**: pertence a um único processo. Não há escalonador, tick nem interrupção nesse núcleo; o processo é dono do tempo dele.
  - **Compartilhado**: várias tarefas dividem o núcleo por prioridade fixa (D11). A garantia forte de tempo do sistema está nos núcleos dedicados.

## 3. Requisitos

### Funcionais

- **RF1** — Alocar um núcleo com exclusividade a um processo.
- **RF2** — Rodar tarefas concorrentes nos núcleos compartilhados, com prioridade fixa por tarefa (D11).
- **RF3** — (retirado pela D11: sem contratos, não há teste de admissão.)
- **RF4** — Conter a tarefa de núcleo compartilhado que estoura o seu limite de tempo de CPU, sem afetar as outras (D11a).
- **RF5** — Isolar processos entre si e do kernel; a falha de um não derruba o sistema.
- **RF6** — Trocar dados entre o núcleo dedicado e os compartilhados sem bloquear o lado de tempo real.
- **RF7** — Medir e expor latência, jitter, uso de orçamento e deadlines perdidos, por núcleo e por tarefa.
- **RF8** — Acessar a E/S de automação (D7): pinos de entrada e saída, serial e SPI para os processos de tempo real; essas e também I2C e Ethernet para os processos de núcleo compartilhado. Analógico e CAN dependem de chips externos em SPI.
- **RF9** — Detectar que o processo dedicado perdeu o passo e levar as saídas dele a um estado seguro.
- **RF11** — Oferecer uma interface web para operar e monitorar o sistema, sem perturbar os processos de tempo real.
- **RF12** — Oferecer, na interface web, uma página de estatísticas do sistema com o básico geral, medido pelo próprio kernel. Os requisitos detalhados estão em `ESTATISTICAS.md`, em revisão.
- **RF10** — Dar ao processo dedicado acesso direto ao contador de tempo e aos registradores dos seus dispositivos, sem chamada ao kernel.

### Tempo real

- **RT1** — Em operação normal, nenhuma interrupção chega ao núcleo dedicado, nem as do kernel.
- **RT2** — O passo de tempo e a variação de cada borda gerada pelo processo dedicado são os menores que o hardware permite. Não há meta numérica: o valor é o que a medição na placa mostrar, e o trabalho é aproximá-lo do limite físico.
- **RT3** — Toda operação do kernel tem tempo limitado por constantes de configuração, nunca pela carga.
- **RT4** — Nos núcleos compartilhados, a tarefa de maior prioridade pronta assume a CPU em tempo limitado e conhecido. O cumprimento de prazos pelas demais depende da atribuição de prioridades, que é responsabilidade de quem configura a instalação.
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
- **Decidido (D3a)**: a instalação é descrita por um manifesto no cartão, lido pelo carregador no boot. Os núcleos 1 a 3 ficam parados até a leitura. O kernel valida cada pedido (permissões, prioridades) e, terminada a partida, a configuração é selada até o desligamento. Manifesto ausente ou inválido: o sistema não parte e relata o erro no console.
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

### Núcleos compartilhados

- **Decidido (D11)**: prioridade fixa com preempção, por núcleo. A prioridade de cada tarefa vem do manifesto e não muda em execução.
- Um evento de dispositivo acorda a tarefa do driver, que roda na hora se for a de maior prioridade pronta.
- Proposta: ativações periódicas em instantes absolutos do contador comum, para que as tarefas fiquem em fase com o resto do sistema.
- Proposta: timer em modo one-shot, de modo que o núcleo só é interrompido no próximo evento que importa.
- **Decidido (D11a)**: toda tarefa tem, além da prioridade, um período e um limite de tempo de CPU por período. O manifesto pode declará-los; quando não declara, vale um limite default. O kernel conta o tempo usado e suspende até o período seguinte a tarefa que estourar; o estouro é registrado. Não existe tarefa sem limite, o que protege a supervisão dos núcleos dedicados feita no núcleo 0.
- Proposta: o valor do limite default é definido uma vez, em uma linha geral do próprio manifesto, e o kernel traz um valor de fábrica para quando essa linha faltar.
- **Decidido (D11b)**: no boot, o sistema soma os limites das tarefas de cada núcleo compartilhado, cada um como fração do período da tarefa. Se a soma passar de 100%, os limites são reduzidos na mesma proporção até caberem. O sistema parte com os limites reduzidos.
- Consequência: depois de um rateio, uma tarefa recebe menos tempo do que declarou. Se ela precisar de fato do que declarou, será suspensa por estouro a cada período. O rateio e os limites efetivos de cada tarefa são relatados no console, para que isso não passe despercebido.
- A soma abaixo de 100% não garante, sozinha, que toda tarefa termine dentro do próprio período; isso continua dependendo das prioridades (RT4).
- Proposta: o teto do rateio fica um pouco abaixo de 100%, para cobrir o tempo gasto pelo próprio kernel; o valor é definido junto com o limite default.
- **Decidido (D11c)**: o rateio vale para todas as tarefas. As do sistema (supervisão dos núcleos dedicados, conjunto de partida) têm um limite mínimo: o rateio pode reduzi-las até esse piso, não além. O que o piso preserva é descontado das demais tarefas, para a soma continuar cabendo.
- Proposta: os pisos são definidos na imagem do kernel, junto com as tarefas do sistema, e não no manifesto; assim um manifesto exagerado não consegue rebaixá-los.

### Isolamento

- Kernel em EL1, processos em EL0, cada um com seu espaço de endereçamento (MMU).
- **Decidido (D12)**: as permissões são uma tabela fixa. O manifesto declara, para cada processo, os dispositivos, as regiões de memória e os canais que ele usa. O kernel mapeia tudo no boot e sela; depois disso, o que foi mapeado é o que existe, e o kernel não confere permissões em execução.
- Um mesmo dispositivo declarado como exclusivo por dois processos é erro de manifesto: o sistema não parte.
- Kernel pequeno: tempo, memória e comunicação.
- **Decidido (D5)**: os drivers de E/S ficam nos processos. O kernel mantém apenas o timer, o controlador de interrupções e um console mínimo de depuração.
- **Decidido (D5)**: um dispositivo exclusivo é mapeado no processo dono, que o acessa direto. Um dispositivo compartilhado nunca é mapeado no processo de tempo real: ele fala com o kernel, que é a autoridade sobre o acesso.
- **Decidido (D5a, D12)**: o canal entre o processo e o driver é declarado no manifesto e criado pelo kernel no boot. Na partida, antes do laço de tempo real, o processo pede ao kernel a ligação ao canal que lhe foi atribuído. Chamadas diretas ao kernel ficam para configuração, fora do laço.

### Comunicação entre núcleos

- **Decidido (D5a)**: canais de memória compartilhada, sem trava, criados e controlados pelo kernel.
- Quem está no núcleo dedicado nunca espera pelo outro lado; a resposta de um pedido chega em um ciclo posterior, pela fila de volta.
- Proposta: um produtor e um consumidor por canal. A regra para fila cheia (descartar e contar, por exemplo) ainda precisa ser definida.

### Interface web e observabilidade

- **Decidido (D14)**: a interface web é servida pela própria placa, por Ethernet. O driver de rede, a pilha de protocolos e o servidor web são processos de núcleo compartilhado, com prioridade e limite de tempo, e só falam com os núcleos dedicados por canais.
- **Decidido (D16)**: a pilha de protocolos é a lwIP, adaptada. Ela é código de terceiros e não segue as regras da D9; isso é aceito porque roda isolada em um processo, onde uma falha derruba a rede e não o sistema. A licença BSD permite o uso em código fechado e exige manter o aviso de autoria.
- Nas primeiras etapas, antes de a rede existir, o monitoramento sai pela serial, em texto.
- **Decidido (D13) — interface de escrita**: no boot, o kernel cria um buffer circular de telemetria para cada processo e define o formato dos registros. Empurrar um dado é gravar um registro (identificador, valor, instante) nesse buffer, direto na memória, sem entrada no kernel. A função é a mesma para todos os processos. O processo nunca espera: com o buffer cheio, o registro mais antigo é sobrescrito e a perda é contada.
- **Decidido (D13) — interface de leitura**: quem precisa dos dados e dos contadores os obtém por uma interface de leitura do kernel, separada da de escrita. Quais dados ler e com que frequência é escolhido em execução.
- Os contadores do próprio kernel (ativações, estouros de limite, rateio aplicado, por tarefa e por núcleo) ficam disponíveis pela mesma interface de leitura.
- Proposta: dois tipos de dado, valor atual de uma variável e evento com instante.
- Proposta: na partida, antes do laço, o processo registra cada variável com nome, tipo e unidade, por chamada normal ao kernel; é o que permite à interface web descobrir o que existe.
- Proposta: o direito de ler os dados de outros processos é declarado no manifesto (D12).
- O custo de um envio no laço de tempo real não foi medido e entra no ensaio de variação de borda da etapa 4.
- **Decidido (D15) — interface de parâmetros**: é o caminho de volta, espelho da D13. Na partida, o processo declara cada parâmetro com nome, tipo, faixa permitida e valor inicial. A escrita é feita por uma interface do kernel, que recusa valor fora da faixa e escrita sem permissão; o direito de escrita é declarado no manifesto (D12). O processo lê o valor atual direto da memória, no ponto do ciclo que escolher, sem entrar no kernel e sem esperar.
- Proposta: parâmetros que precisam mudar juntos são gravados em grupo, e o processo passa a ver o grupo novo inteiro de uma vez, nunca pela metade.
- Pendente para a etapa da interface web: controle de quem pode acessar a interface e alterar parâmetros pela rede.

### Regras de codificação (decidido — D9)

- Sem recursão, para a pilha ter tamanho máximo conhecido.
- Todo laço com limite fixo, para o tempo de execução ser limitado (RT3).
- Sem alocação dinâmica de memória depois da partida.
- Tipos de tamanho fixo e nenhum comportamento indefinido da linguagem.
- Todo valor de retorno conferido; avisos do compilador tratados como erro.
- Verificação automática a cada compilação, com o compilador e os analisadores estáticos do LLVM.
- A lista é inspirada na MISRA C e nas regras da NASA para código crítico, sem pretensão de certificação. As exceções inevitáveis em código de kernel (endereços de registradores, assembly) ficam restritas a arquivos identificados.

## 5. Etapas

Cada etapa termina com algo verificado na placa. Por decisão do usuário, a primeira meta é **o sistema no ar e acessível pelo navegador**: o kernel rodando mais o serviço web. As etapas 1 a 5 levam até lá pelo caminho mais curto que respeita a arquitetura; os núcleos dedicados vêm em seguida.

| Etapa | Entrega | Critério de aceite |
|-------|---------|--------------------|
| 0. Bancada | Toolchain, emulador, gravação do cartão SD, console serial | Mensagem de boot lida na serial da placa física |
| 1. Base | MMU e caches, exceções, GIC, timer | Interrupção periódica do timer tratada na placa, com a latência medida pelo contador e registrada |
| 2. Processos e prioridades | Processos em EL0 com espaço de endereçamento próprio e chamadas ao kernel; prioridade fixa com limite de tempo e rateio, no núcleo 0 (D2 em parte, D11 a D11c) | Processo que acessa memória alheia é encerrado e o resto segue; tarefa de alta prioridade em laço infinito é suspensa ao estourar o limite; soma acima de 100% parte com os limites rateados e relatados no console |
| 3. Dispositivos em processo | Tabela fixa de permissões, dispositivo mapeado no processo e entrega de interrupção ao processo (D5, D12) | Processo que tenta acessar um dispositivo fora da sua tabela é encerrado; dispositivo exclusivo declarado por dois processos impede a partida |
| 4. Rede | Driver da Ethernet e adaptação da lwIP, em processos de núcleo compartilhado (D14, D16) | A placa responde a `ping` na rede |
| 5. Serviço web | Servidor web e primeira página, com o estado do sistema (RF11) | Navegador na rede abre a página servida pela placa |
| 6. Observabilidade e parâmetros | Interfaces de escrita e de leitura de dados e interface de parâmetros do kernel (D13, D15) | A página mostra dados empurrados por um processo e os contadores do kernel; um parâmetro alterado pela página passa a valer no processo, e valor fora da faixa é recusado |
| 7. Multicore | Partida dos 4 núcleos, estado por núcleo, interrupção entre núcleos | Os 4 núcleos rodando código próprio, visíveis na página |
| 8. Piso do hardware e núcleo dedicado | Ensaio de troca de pino em laço; núcleo sem interrupções entregue a um processo; PWM por software; supervisão por batimento | Passo de tempo, taxa máxima de troca do pino e variação de borda medidos com osciloscópio e registrados; a variação não muda com os outros 3 núcleos sob carga máxima nem com tráfego de rede intenso; processo travado é detectado e a saída vai ao estado seguro |
| 9. Canais | Comunicação sem trava entre núcleo dedicado e compartilhados | Troca de dados sem alterar a variação de borda medida na etapa 8 |
| 10. Carga do cartão | Driver do cartão SD, leitura de FAT, carregador de programas e leitura do manifesto (D6, D3a) | Processo gravado como arquivo no cartão é carregado e executa; cartão ausente ou arquivo corrompido resulta em falha relatada, sem travar o sistema |
| 11. E/S | Drivers de GPIO, serial, SPI e I2C em espaço de processo (D7) | Malha de controle real fechada pelo núcleo dedicado, com leitura por SPI e comando pela serial; sensor I2C lido por um processo de núcleo compartilhado |

Até a etapa 10 existir, dois recursos provisórios substituem o cartão: os programas entram na memória embutidos na imagem do kernel, e a descrição da instalação (prioridades, limites, permissões) é uma tabela dentro da imagem, com o mesmo conteúdo que o manifesto terá. O mecanismo de embutir programas fica em definitivo para o conjunto de partida (D6a).

Essa ordem segue a D17: o serviço web já nasce como processo.

## 6. Decisões em aberto

Discutidas uma por vez. As decisões D2 a D16 foram fechadas e estão na seção 1. Resta a D1, que depende de medição, e a confirmação dos itens da seção 4 ainda marcados como proposta.

| # | Decisão | Recomendação |
|---|---------|--------------|
| D1 | Metas numéricas para os núcleos compartilhados (latência, jitter) | Fixar depois da medição da etapa 1 |

## 7. Bancada necessária

- Adaptador USB-serial de 3,3 V, para o console na placa (pinos 6, 8 e 10 do conector GPIO). Sem ele não há saída visível na placa, pois ainda não existe driver de vídeo.
- Cartão microSD e leitor.
- Osciloscópio, com banda suficiente para ver bordas de dezenas de nanossegundos. É obrigatório: RT2 e o critério da etapa 8 só se verificam medindo o pino por fora.
- Cabo de rede e um PC ou switch na mesma rede, a partir da etapa 4.

O usuário confirmou em 2026-10-02 que dispõe de todos esses itens.

## 8. Riscos

- **Interferência entre núcleos pelo hardware**: os 4 núcleos dividem a cache L2 e o barramento de memória e de periféricos. Um núcleo compartilhado sob carga pode deslocar bordas do dedicado mesmo sem nenhuma interferência do kernel. É o maior risco técnico para RT2 e só aparece na placa.
- **Firmware da GPU**: o firmware da Broadcom continua ativo depois do boot e controla clocks e temperatura. Se ele alterar a frequência da CPU, a contagem de ciclos deixa de servir como base de tempo. O efeito sobre o determinismo é desconhecido.
- **Caminho até o pino**: a escrita no registrador de GPIO atravessa um barramento cuja latência e variação não são conhecidas. Elas definem o piso real da variação de borda.
- **O emulador não reproduz tempo**: o QEMU valida lógica, não latência. Toda afirmação de tempo real precisa de medição na placa.
- **O emulador não tem a placa de rede do Pi 4**: o QEMU 11.1 não emula o controlador Ethernet (GENET) na máquina `raspi4b`; verificado no executável instalado. As etapas 4 e 5 (rede e serviço web) só podem ser testadas na placa física, o que torna cada ciclo de teste mais lento e deixa o driver da Ethernet sem rede de segurança.
- **Periféricos difíceis**: no Pi 4, o USB fica atrás de um controlador PCIe e está fora do plano. A Ethernet exige driver próprio e uma pilha de protocolos; é o maior bloco de trabalho fora do núcleo do sistema, e passou para o início do plano por ser a primeira meta de implementação.
- **Cartão SD no caminho do boot (D6)**: o sistema passa a depender de um driver de cartão, que é trabalhoso e varia com o cartão usado, e a leitura vira um ponto de falha na partida. Leituras durante a operação usam o barramento compartilhado e podem interferir nos núcleos dedicados; o efeito precisa ser medido.

## 9. Estado atual

**Etapas 0 e 1 — implementadas e verificadas no emulador; aguardam o teste na placa física.**

- O kernel (pasta `kernel/`) dá boot em EL1, inicializa o console na UART0 e imprime a identificação do sistema.
- Liga a MMU e as caches com mapa identidade e permissões por seção: código só para leitura e execução, constantes só para leitura, dados e RAM sem execução, periféricos sem cache.
- Inicializa o GIC e o timer, que interrompe 1000 vezes por segundo em instantes absolutos. A cada segundo, o console mostra o atraso de tratamento medido pelo contador (mínimo, média, máximo e pior caso), os períodos pulados e as interrupções sem tratador.
- Qualquer exceção inesperada é relatada no console e para o sistema.
- O código segue as regras da D9: compila com todos os avisos tratados como erro e passa pelo analisador estático (`clang-tidy`, configurado em `.clang-tidy`) a cada compilação.
- Verificado no QEMU 11.1 (`raspi4b`), com `scripts/test-qemu.ps1`:
  - com relógio virtual, 1000 disparos por segundo, atraso constante de 48 ns e nenhum período pulado;
  - com o relógio do Windows, atrasos de 1 a 2 ms e períodos pulados, causados pelo agendador do Windows e não pelo kernel.
- Verificado no QEMU, com `scripts/test-fault.ps1`: uma gravação no código do kernel é barrada pela MMU com falha de permissão.
- Verificado: `scripts/build.ps1` e `scripts/make-sdcard.ps1` sem a opção `-Drive`, que monta o conteúdo do cartão em `build/sdcard` com o firmware baixado do repositório oficial da Raspberry Pi.
- **Não verificado**: execução na placa física, e portanto os critérios de aceite das etapas 0 e 1; a cópia para o cartão (`make-sdcard.ps1 -Drive`); `scripts/run-qemu.ps1` em uso interativo.
- O rascunho exploratório anterior ao plano (EDF em um núcleo) foi retirado da árvore; continua no histórico, no commit `93671bb`.
- Ferramentas instaladas no Windows: LLVM/clang 23.1.2 e QEMU 11.1.0.
