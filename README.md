# ProcessFlow

Orquestrador de processos escrito em C para a disciplina de Infraestrutura de Software
(CESAR School). O programa cadastra tarefas que representam programas do sistema e as
executa criando e gerenciando seus próprios processos filhos.

Toda a execução é feita com chamadas de sistema diretas — `fork()`, `execvp()`,
`wait()`/`waitpid()`, `dup2()` e `pipe()`. **Não há uso de `system()`, `popen()` ou
delegação a outro shell.**

---

## Sistema operacional

Desenvolvido e testado em **Ubuntu 24.04 LTS sobre WSL2 (Windows 11)**.

| Ferramenta | Versão |
|---|---|
| gcc | 13.3.0 |
| GNU Make | 4.3 |
| git | 2.43.0 |

Deve compilar em qualquer sistema Linux, Unix ou macOS com um compilador C11 e `make`.

---

## Arquivos e responsabilidades

| Arquivo | Responsabilidade |
|---|---|
| `main.c` | `main()`, validação dos argumentos, escolha entre modo interativo e modo workflow, e os laços de leitura de linha |
| `parser.c` / `parser.h` | Quebra a linha em palavras (tolerante a múltiplos espaços e a CRLF) e despacha para o comando correspondente |
| `tasks.c` / `tasks.h` | `struct Task`, tabela de tarefas cadastradas, cadastro com cópia própria dos argumentos e busca por nome |
| `executor.c` / `executor.h` | Único ponto de criação e coleta de processos: `fork`, `execvp`, `waitpid`, e os modos de execução simples, sequencial e paralelo |
| `Makefile` | Alvos `all`, `test`, `clean` e `entrega` |
| `teste1.pf` | Workflow de exemplo, com casos de erro incluídos de propósito |
| `teste_seq.pf` / `teste_par.pf` | Workflows usados para comparar o tempo de execução sequencial e paralela |

Os arquivos objeto (`.o`) são gerados dentro de `build/`, que é criado automaticamente
pelo `make` e removido pelo `make clean`.

---

## Como compilar

```bash
make clean
make
```

A compilação usa `-Wall -Wextra` e não deve produzir nenhum aviso.

---

## Como executar

**Modo interativo** — apresenta o prompt `processflow>`:

```bash
./processflow
```

**Modo workflow** — lê os comandos de um arquivo, sem exibir o prompt, imprimindo cada
linha antes de processá-la:

```bash
./processflow teste1.pf
```

Passar mais de um argumento é erro e encerra o programa com código diferente de zero.

---

## Comandos disponíveis

| Comando | O que faz |
|---|---|
| `task <nome> <programa> [args...]` | Cadastra uma tarefa. Nada é executado neste momento |
| `run <nome>` | Executa a tarefa e espera ela terminar |
| `run sequential <t1> <t2> ...` | Executa em fila: cada tarefa só começa quando a anterior termina |
| `run parallel <t1> <t2> ...` | Cria todos os processos antes de esperar por qualquer um |
| `run pipe <t1> <t2> ...` | Liga a saída de cada tarefa à entrada da seguinte; todos os processos rodam ao mesmo tempo |
| `input <tarefa> <arquivo>` | A tarefa passa a ler a entrada do arquivo |
| `output <tarefa> <arquivo>` | A tarefa passa a gravar a saída no arquivo, sobrescrevendo |
| `append <tarefa> <arquivo>` | Igual ao `output`, mas escrevendo no fim do arquivo |
| `exit` | Encerra o ProcessFlow |

Os arquivos de `input`, `output` e `append` são verificados no momento em que o comando é
digitado: se não puderem ser abertos, o erro é reportado e a configuração não é gravada.
Recadastrar uma tarefa com `task` zera os redirecionamentos dela.

Numa cadeia de `run pipe`, o `input` da **primeira** tarefa e o `output`/`append` da
**última** são respeitados; nas tarefas do meio o pipe tem prioridade, já que as duas
pontas delas estão ocupadas.

Pressionar **Ctrl+D** no modo interativo tem o mesmo efeito de `exit`.

Linhas vazias, linhas contendo apenas espaços e sequências de múltiplos espaços entre
palavras são tratadas normalmente, sem gerar erro.

### Exemplo

```
$ ./processflow
processflow> task listar /bin/ls -l
tarefa 'listar' cadastrada
processflow> run listar
total 212
-rw-r--r-- 1 fonse fonse  2632 main.c
...
processflow> task lento /bin/sleep 3
processflow> task rapido /bin/sleep 1
processflow> run parallel lento rapido
processflow> exit
```

---

## Tratamento de erros

**Imprimem mensagem e encerram o programa:**

| Situação | Como reproduzir |
|---|---|
| Número incorreto de argumentos | `./processflow a b c` |
| Arquivo de workflow não pode ser aberto | `./processflow naoexiste.pf` |

**Imprimem mensagem e continuam processando:**

| Situação | Como reproduzir |
|---|---|
| Tarefa não cadastrada | `run fantasma` |
| Programa não existe | `task x /bin/nao_existe` seguido de `run x` |
| Programa sem permissão de execução | `task y /etc/hostname` seguido de `run y` |
| Arquivo de entrada não pode ser aberto | `input ordenar /caminho/que/nao/existe.txt` |
| Arquivo de saída não pode ser aberto | `output ordenar /proc/impossivel.txt` |
| Comando desconhecido | `comando_inventado` |
| `task` sem programa informado | `task solta` |

---

## Como testar

```bash
make test
```

A bateria executa, em sequência: o modo workflow com `teste1.pf`, a comparação de tempo
entre execução sequencial e paralela, os dois erros fatais com seus códigos de saída, e
a verificação de processos zumbis.

**Comprovação do paralelismo.** As tarefas usadas são `/bin/sleep 3` e `/bin/sleep 1`,
que não produzem saída — a evidência é o tempo total:

```bash
time ./processflow teste_seq.pf    # ~4s: 3s + 1s, um depois do outro
time ./processflow teste_par.pf    # ~3s: apenas o tempo da tarefa mais demorada
```

**Verificação de processos zumbis:**

```bash
ps -ef | grep defunct
```

Não deve retornar nenhum processo `<defunct>`.

---

## Como empacotar para entrega

```bash
make entrega
```

O alvo limpa os arquivos gerados, verifica que não há chamadas a `system()` ou `popen()`
no código-fonte, gera `../maf.tar` e lista o conteúdo do pacote para conferência.
