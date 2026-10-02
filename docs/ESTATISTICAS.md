# RTR-OS — Requisitos da página de estatísticas

Situação: **proposta para revisão**. Expande o pedido do usuário de 2026-10-02: o servidor web deve ter uma página de estatísticas do sistema, com o básico geral, medido pelo próprio kernel. Complementa `PLANO.md` (RF7, RF11, D13, D15).

## 1. Propósito

A página existe para responder, em um relance, a três perguntas:

1. **O sistema está saudável?** Nada falhou, nada foi perdido, nada foi contido.
2. **O sistema está cumprindo o tempo?** Os prazos estão sendo atendidos, e com que folga.
3. **O que aconteceu?** Qual foi o pior caso desde que ligou, e quando.

Ela é a vista do sistema operacional sobre si mesmo. Dados das aplicações têm páginas próprias, alimentadas pelo mesmo mecanismo (D13).

## 2. Princípios

- **P1 — O kernel mede; a página mostra.** Toda estatística tem uma única fonte: um valor mantido pelo kernel e lido pela interface de leitura (D13). A página não calcula nada que o kernel possa medir, e nenhum outro componente mantém uma cópia concorrente.
- **P2 — Medir não perturba.** Registrar uma medida custa um número pequeno e constante de operações, sem trava e sem espera. Nenhuma medida gera interrupção em núcleo dedicado.
- **P3 — O pior caso vale tanto quanto a média.** Em tempo real, a média esconde o problema. Toda grandeza de tempo é mostrada com mínimo, média, máximo e pior caso desde o boot.
- **P4 — Toda medida se explica.** Cada valor tem nome, unidade, escopo e o instante em que foi lido. Um número sem unidade não aparece na página.
- **P5 — Ausência é ausência.** O que não foi medido aparece como "sem dado", nunca como zero nem como estimativa. Um dado antigo aparece marcado como antigo.
- **P6 — Um só relógio.** Todos os instantes e durações vêm do contador do sistema, movido pelo cristal. A página não usa o relógio do navegador para nada além de desenhar.
- **P7 — Observar não é operar.** A página de estatísticas é somente leitura. A única ação que ela oferece é zerar os piores casos, e essa ação fica registrada como evento.

## 3. Modelo de uma medida

Toda estatística pertence a um de cinco tipos. O tipo define como o kernel a guarda e como a página a mostra.

| Tipo | O que é | Como é guardada | Como é mostrada |
|------|---------|-----------------|-----------------|
| Contador | Total que só cresce (interrupções, bytes, estouros) | Inteiro de 64 bits, desde o boot | Total e taxa por segundo, que a página deriva de duas leituras |
| Nível | Valor do momento (memória em uso, ocupação da fila) | Valor atual e máximo já atingido | Valor, máximo e limite, quando houver |
| Duração | Tempo de algo que se repete (atraso, tempo de resposta) | Mínimo, soma e máximo da janela; pior caso desde o boot, com o instante em que ocorreu | Mínimo, média, máximo e pior caso |
| Estado | Situação entre um conjunto fixo (pronto, suspenso, encerrado) | Valor atual e instante da última mudança | Nome do estado e há quanto tempo |
| Evento | Algo que aconteceu uma vez (falha, rateio, parada segura) | Registro com instante, tipo e origem, em buffer circular | Lista do mais recente para o mais antigo |

Cada medida tem um **escopo**: sistema, núcleo, tarefa, interrupção, dispositivo ou canal.

Duas janelas de tempo valem para todas as durações:

- **Janela de leitura**: do último pedido de leitura até o atual. Cada leitor tem a sua; ler não altera o que outro leitor vê.
- **Desde o boot**: nunca é reiniciada por uma leitura. Os piores casos só voltam a zero por comando explícito (P7).

## 4. Catálogo de medidas

### 4.1 Identidade e tempo

| Medida | Tipo | Unidade |
|--------|------|---------|
| Nome e versão do RTR-OS, instante da compilação | Estado | — |
| Modelo e revisão da placa, número de série | Estado | — |
| Tempo ligado | Contador | s |
| Frequência do contador do sistema | Nível | Hz |
| Motivo da última partida (energização, reinício pedido, parada por falha) | Estado | — |

### 4.2 Processador, por núcleo

| Medida | Tipo | Unidade |
|--------|------|---------|
| Modo do núcleo (sistema, compartilhado, dedicado, parado) | Estado | — |
| Ocupação: fração do tempo fora do ocioso | Nível | % |
| Trocas de contexto | Contador | — |
| Interrupções tratadas | Contador | — |
| Entradas no kernel por chamada de processo | Contador | — |
| Maior trecho contínuo com interrupções mascaradas | Duração | ns |

A última linha é a medida direta do requisito RT3 (toda operação do kernel tem tempo limitado): é o tempo máximo em que o kernel ficou surdo.

Para núcleo **dedicado**, o kernel não roda lá em operação normal (D2) e por isso não mede nada lá dentro. O que aparece é o que o núcleo 0 observa de fora:

| Medida | Tipo | Unidade |
|--------|------|---------|
| Processo dono do núcleo | Estado | — |
| Intervalo entre batimentos | Duração | ns |
| Batimentos perdidos | Contador | — |
| Estado da supervisão (normal, batimento perdido, saída em estado seguro) | Estado | — |

### 4.3 Tempo

| Medida | Escopo | Tipo | Unidade |
|--------|--------|------|---------|
| Atraso de tratamento do timer (do instante programado à entrada do tratador) | Núcleo | Duração | ns |
| Períodos do timer pulados | Núcleo | Contador | — |
| Latência de interrupção (da chegada ao início do tratador ou da tarefa do driver) | Interrupção | Duração | ns |

### 4.4 Tarefas dos núcleos compartilhados

Uma linha por tarefa.

| Medida | Tipo | Unidade |
|--------|------|---------|
| Nome, núcleo, prioridade, período | Estado | — |
| Limite de tempo declarado e limite efetivo depois do rateio (D11b) | Nível | µs |
| Estado (pronta, rodando, aguardando, suspensa por estouro, encerrada por falha) | Estado | — |
| Ativações e conclusões | Contador | — |
| Tempo de CPU usado por período | Duração | µs |
| Uso do limite: tempo usado sobre limite efetivo | Nível | % |
| Estouros de limite (D11a) | Contador | — |
| Tempo de resposta (da ativação à conclusão) | Duração | µs |
| Períodos em que a tarefa não concluiu antes da ativação seguinte | Contador | — |
| Maior uso da pilha | Nível | bytes |

No escopo do sistema: se houve rateio na partida e por qual fator (D11b), e quais tarefas ficaram no piso (D11c).

### 4.5 Memória

Como não há alocação dinâmica depois da partida (D9), o mapa de memória é fixo. A página o mostra uma vez e acompanha só o que varia.

| Medida | Escopo | Tipo | Unidade |
|--------|--------|------|---------|
| Memória total da placa | Sistema | Nível | bytes |
| Memória do kernel: código, constantes, dados, tabelas de tradução | Sistema | Nível | bytes |
| Memória mapeada | Processo | Nível | bytes |
| Memória não atribuída | Sistema | Nível | bytes |
| Maior uso da pilha do kernel | Núcleo | Nível | bytes |
| Falhas de acesso à memória | Processo | Contador | — |

### 4.6 Processos

| Medida | Tipo | Unidade |
|--------|------|---------|
| Nome, origem (conjunto de partida ou cartão), núcleo | Estado | — |
| Estado e instante da última mudança | Estado | — |
| Reinícios | Contador | — |
| Última falha: tipo, endereço e instante | Evento | — |
| Dispositivos e canais atribuídos (D12) | Estado | — |

### 4.7 Interrupções e dispositivos

| Medida | Escopo | Tipo | Unidade |
|--------|--------|------|---------|
| Ocorrências | Interrupção | Contador | — |
| Processo dono | Interrupção | Estado | — |
| Interrupções sem tratador | Sistema | Contador | — |
| Serial: bytes enviados e recebidos, erros de recepção, caracteres descartados | Dispositivo | Contador | — |
| Rede: quadros enviados e recebidos, erros, descartes | Dispositivo | Contador | — |
| Rede: estado e velocidade do enlace | Dispositivo | Estado | — |

As medidas de dispositivo são mantidas pelo driver, que é um processo (D5), e chegam à página pela interface de escrita de dados (D13). As demais são do kernel.

### 4.8 Comunicação

| Medida | Escopo | Tipo | Unidade |
|--------|--------|------|---------|
| Mensagens enviadas e recebidas | Canal | Contador | — |
| Ocupação da fila | Canal | Nível | mensagens |
| Mensagens descartadas por fila cheia | Canal | Contador | — |
| Registros de dados empurrados e sobrescritos antes de lidos (D13) | Processo | Contador | — |
| Escritas de parâmetro aceitas e recusadas (D15) | Processo | Contador | — |

### 4.9 Saúde e eventos

O **registro de eventos** guarda, com instante, tudo o que foge do funcionamento normal:

- partida do sistema e rateio aplicado;
- estouro de limite de uma tarefa;
- falha e reinício de processo;
- batimento perdido e passagem ao estado seguro;
- erro de manifesto;
- piores casos zerados por comando.

O **estado geral** do sistema é derivado desses dados por regras fixas, visíveis na própria página:

| Estado | Regra |
|--------|-------|
| Normal | Nenhuma das condições abaixo |
| Atenção | Na janela de leitura houve estouro de limite, descarte ou perda de dados, ou o sistema partiu com rateio |
| Falha | Há processo encerrado por falha, batimento perdido ou saída em estado seguro |

## 5. Requisitos da coleta, no kernel

- **EK1** — Cada medida do catálogo com origem no kernel é mantida pelo kernel e exposta pela interface de leitura (D13).
- **EK2** — Atualizar uma medida custa um número fixo de operações, independente da carga e do número de tarefas.
- **EK3** — As medidas são guardadas por núcleo e atualizadas sem trava; um núcleo nunca espera por outro para registrar.
- **EK4** — Contadores têm 64 bits e não voltam a zero durante a vida do sistema.
- **EK5** — Uma leitura devolve um conjunto coerente: os valores de uma mesma tarefa ou de um mesmo núcleo pertencem ao mesmo instante, e esse instante acompanha a resposta.
- **EK6** — Ler não altera os valores desde o boot nem a janela de outro leitor.
- **EK7** — Os piores casos só voltam a zero por comando explícito, que gera um evento.
- **EK8** — O registro de eventos é circular e de tamanho fixo. Quando enche, o evento mais antigo é sobrescrito e o total de eventos perdidos é uma medida do catálogo.
- **EK9** — Nenhuma medida exige interromper ou instrumentar um núcleo dedicado. O que se sabe dele vem do batimento observado de fora e dos dados que o próprio processo empurra.
- **EK10** — A coleta existe desde as primeiras etapas, antes do serviço web. Enquanto não há rede, o mesmo conteúdo sai pelo console serial.

## 6. Requisitos da página

- **EP1** — A primeira tela mostra, sem rolagem em um monitor comum: estado geral, tempo ligado, ocupação de cada núcleo, pior atraso do timer e os eventos mais recentes.
- **EP2** — As demais seções seguem o catálogo: núcleos, tempo, tarefas, memória, processos, interrupções e dispositivos, comunicação, eventos.
- **EP3** — A página se atualiza sozinha, sem recarregar. O intervalo padrão é de 1 s e pode ser alterado por quem está vendo.
- **EP4** — Todo valor aparece com unidade. Durações aparecem com mínimo, média, máximo e pior caso, e o pior caso informa quando ocorreu.
- **EP5** — Contadores de coisas que não deveriam acontecer (estouros, perdas, descartes, falhas) ficam em destaque quando são diferentes de zero.
- **EP6** — A página mostra o instante da última leitura. Se o sistema deixar de responder, os valores ficam marcados como antigos em vez de continuarem com aparência de atuais.
- **EP7** — Os mesmos dados ficam disponíveis em formato para leitura por programa (JSON), no mesmo endereço base, para uso por outras ferramentas.
- **EP8** — A página guarda no navegador um histórico curto (os últimos minutos) para desenhar a evolução da ocupação e dos atrasos. O sistema não armazena histórico.
- **EP9** — Servir a página não pode afetar o que ela mede. O serviço web roda com prioridade e limite de tempo (D11a); se o limite for atingido, a página fica mais lenta e os dados continuam corretos.
- **EP10** — A página funciona sem depender de nenhum recurso externo à placa: nenhum arquivo é buscado na internet.

## 7. Entrega por etapa

A página nasce pequena e cresce com o sistema. Cada etapa do plano acrescenta as medidas que passam a existir.

| Etapa do plano | Medidas disponíveis |
|----------------|---------------------|
| 1. Base | Identidade e tempo; atraso do timer; interrupções. Saída pelo console serial |
| 2. Processos e prioridades | Tarefas, processos, memória, trecho mascarado |
| 3. Dispositivos em processo | Interrupções por dono, latência de interrupção, serial |
| 4. Rede | Rede |
| 5. Serviço web | Primeira versão da página, com tudo o que há até aqui (EP1 a EP7) |
| 6. Observabilidade e parâmetros | Comunicação, dados de aplicação, histórico no navegador (EP8) |
| 7. Multicore | Medidas por núcleo para os quatro núcleos |
| 8. Núcleo dedicado | Batimento e supervisão |

## 8. Fora do escopo por enquanto

- Histórico armazenado no sistema ou em cartão.
- Alarmes enviados para fora (e-mail, mensagens).
- Medição de tempo por função do código.
- Controle de acesso à página, que será tratado junto com o da interface web (pendência registrada na D15).

## 9. Pontos em aberto

| # | Ponto | Recomendação |
|---|-------|--------------|
| E1 | Temperatura do processador, frequência real da CPU e alertas de subtensão só são conhecidos pelo firmware da placa, que precisa ser consultado. Entram no catálogo? | Sim: a frequência da CPU é um risco registrado no plano, e esta é a forma de vigiá-lo. Exige um canal de consulta ao firmware, a detalhar |
| E2 | Durações guardam só mínimo, média, máximo e pior caso, ou também a distribuição por faixas (histograma)? | Começar sem histograma; acrescentar para o atraso do timer e o batimento, onde a forma da distribuição importa |
| E3 | Idioma da página | Português, com os textos separados do código para permitir tradução |
| E4 | Os piores casos podem ser zerados por qualquer pessoa que abra a página? | Não: só depois de existir o controle de acesso |
