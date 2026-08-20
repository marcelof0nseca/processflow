#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include "executor.h"

/* ------------------------------------------------------------------ */
/* Funcoes de apoio                                                    */
/* ------------------------------------------------------------------ */

/* Interpreta o status devolvido pelo waitpid.
 *
 * O status NAO e o codigo de saida: e um inteiro com varios campos
 * empacotados. As macros WIFEXITED/WEXITSTATUS/WIFSIGNALED existem
 * justamente para desempacotar isso sem depender do layout dos bits.
 *
 * Terminacao normal com codigo 0 nao imprime nada, para nao poluir a
 * saida do programa que a tarefa executou. */
static int reportar_status(const char *nome, pid_t pid, int status) {
    if (WIFEXITED(status)) {
        int codigo = WEXITSTATUS(status);
        if (codigo != 0) {
            fprintf(stderr, "aviso: tarefa '%s' (pid %d) terminou com codigo %d\n",
                    nome, (int)pid, codigo);
        }
        return codigo;
    }

    if (WIFSIGNALED(status)) {
        /* Terminou por sinal (Ctrl+C, kill, segfault...) e nao por return. */
        fprintf(stderr, "aviso: tarefa '%s' (pid %d) terminou por sinal %d\n",
                nome, (int)pid, WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }

    return -1;
}

/* Aplica os redirecionamentos no processo FILHO, antes do exec.
 * Com -1 nos dois parametros nao faz nada, e o filho herda os descritores
 * do ProcessFlow. */
static void aplicar_redirecionamentos(int fd_entrada, int fd_saida) {
    if (fd_entrada != -1) {
        if (dup2(fd_entrada, STDIN_FILENO) < 0) {
            fprintf(stderr, "erro: dup2 da entrada falhou: %s\n", strerror(errno));
            _exit(126);
        }
        if (fd_entrada != STDIN_FILENO) {
            close(fd_entrada);
        }
    }

    if (fd_saida != -1) {
        if (dup2(fd_saida, STDOUT_FILENO) < 0) {
            fprintf(stderr, "erro: dup2 da saida falhou: %s\n", strerror(errno));
            _exit(126);
        }
        if (fd_saida != STDOUT_FILENO) {
            close(fd_saida);
        }
    }
}

/* Abre os arquivos configurados por input/output/append.
 *
 * A abertura acontece NO PAI, antes do fork. Assim, se o arquivo nao puder
 * ser aberto, a mensagem de erro sai de um lugar so e a execucao e
 * cancelada sem sequer criar o processo filho.
 *
 * Devolve -1 (com mensagem ja impressa) se algum arquivo falhar; nesse caso
 * fecha o que ja tinha aberto, para nao vazar descritor.
 * Em *fd_in e *fd_out volta -1 quando nao ha redirecionamento. */
static int abrir_redirecionamentos(const Task *t, int *fd_in, int *fd_out) {
    *fd_in  = -1;
    *fd_out = -1;

    if (t->arquivo_entrada[0] != '\0') {
        *fd_in = open(t->arquivo_entrada, O_RDONLY);
        if (*fd_in < 0) {
            fprintf(stderr, "erro: nao foi possivel abrir '%s' para leitura (tarefa '%s'): %s\n",
                    t->arquivo_entrada, t->nome, strerror(errno));
            return -1;
        }
    }

    if (t->arquivo_saida[0] != '\0') {
        int flags = O_WRONLY | O_CREAT | (t->saida_append ? O_APPEND : O_TRUNC);

        *fd_out = open(t->arquivo_saida, flags, 0644);
        if (*fd_out < 0) {
            fprintf(stderr, "erro: nao foi possivel abrir '%s' para escrita (tarefa '%s'): %s\n",
                    t->arquivo_saida, t->nome, strerror(errno));
            if (*fd_in != -1) {
                close(*fd_in);
                *fd_in = -1;
            }
            return -1;
        }
    }

    return 0;
}

/* Fecha um descritor se ele estiver aberto. Encurta o codigo em varios
 * pontos onde -1 significa "nao existe". */
static void fechar_se_aberto(int fd) {
    if (fd != -1) {
        close(fd);
    }
}

/* ------------------------------------------------------------------ */
/* Criacao de processo: fork + exec                                    */
/* ------------------------------------------------------------------ */

pid_t task_spawn(const Task *t, int fd_entrada, int fd_saida,
                 const int *fechar_no_filho, int n_fechar) {
    /* Esvazia os buffers do stdio ANTES do fork.
     *
     * Motivo: o fork copia a memoria do processo, inclusive o buffer de
     * saida que ainda nao foi gravado. Se sobrar texto pendente no buffer,
     * pai e filho vao gravar esse mesmo texto, e ele aparece duplicado.
     * No terminal isso quase nunca acontece (a saida e esvaziada a cada
     * '\n'), mas aparece quando a saida vai para um arquivo. */
    fflush(NULL);

    pid_t pid = fork();

    /* Caso 1: fork falhou. Nenhum filho foi criado. */
    if (pid < 0) {
        fprintf(stderr, "erro: nao foi possivel criar processo para a tarefa '%s': %s\n",
                t->nome, strerror(errno));
        return -1;
    }

    /* Caso 2: pid == 0 -> este codigo esta rodando NO FILHO. */
    if (pid == 0) {
        aplicar_redirecionamentos(fd_entrada, fd_saida);

        /* Fecha as pontas de pipe que pertencem a outros elos da cadeia.
         * Isso vem DEPOIS do dup2: os descritores que viraram stdin/stdout
         * ja foram copiados, entao fechar os originais aqui e seguro. */
        for (int i = 0; i < n_fechar; i++) {
            if (fechar_no_filho[i] >= 0) {
                close(fechar_no_filho[i]);
            }
        }

        /* execvp substitui a imagem do processo: daqui em diante o filho E
         * o outro programa. Se der certo, NADA abaixo desta linha executa.
         *
         * O "p" de execvp significa que ele procura o programa no PATH
         * quando o nome nao tem barra. O "v" significa que recebe o argv
         * como vetor terminado em NULL, que e como a Task ja guarda. */
        execvp(t->argv[0], t->argv);

        /* Se a execucao chegou aqui, o exec FALHOU: programa inexistente,
         * sem permissao de execucao, etc. */
        fprintf(stderr, "erro: nao foi possivel executar '%s' (tarefa '%s'): %s\n",
                t->argv[0], t->nome, strerror(errno));

        /* _exit e obrigatorio: sem ele o filho voltaria para o loop de
         * comandos do main e passariam a existir DOIS ProcessFlow lendo do
         * mesmo teclado (comprovado em teste dirigido: ver relatorio).
         * E _exit() e nao exit(): o exit() executaria os handlers e daria
         * flush no buffer herdado do pai, duplicando saida. O 127 e a
         * convencao de shell para "comando nao encontrado". */
        _exit(127);
    }

    /* Caso 3: pid > 0 -> este codigo esta rodando NO PAI, e pid e o
     * identificador do filho recem-criado. */
    return pid;
}

/* ------------------------------------------------------------------ */
/* Coleta de processo                                                  */
/* ------------------------------------------------------------------ */

int task_collect(pid_t pid, const char *nome) {
    int status;

    /* waitpid com o PID exato. Usar waitpid(-1, ...) aqui pegaria
     * "qualquer filho", o que quebraria os jobs em background do Dia 4:
     * o wait de um comando poderia coletar o filho de outro. */
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "erro: waitpid do pid %d falhou: %s\n",
                (int)pid, strerror(errno));
        return -1;
    }

    return reportar_status(nome, pid, status);
}

/* ------------------------------------------------------------------ */
/* Modos de execucao                                                   */
/* ------------------------------------------------------------------ */

int exec_single(const Task *t) {
    int fd_in, fd_out;

    if (abrir_redirecionamentos(t, &fd_in, &fd_out) != 0) {
        return -1;  /* arquivo invalido: erro ja impresso, nada foi criado */
    }

    pid_t pid = task_spawn(t, fd_in, fd_out, NULL, 0);

    /* O pai nao usa esses descritores: o filho ja tem a copia dele. Manter
     * abertos aqui vazaria descritor a cada execucao. */
    fechar_se_aberto(fd_in);
    fechar_se_aberto(fd_out);

    if (pid < 0) {
        return -1;
    }
    return task_collect(pid, t->nome);
}

int exec_sequential(Task *ts[], int n) {
    int ultimo = 0;

    /* Sequencial: exec_single ja faz "criar + esperar" junto, entao chamar
     * ele em laco garante que a tarefa i+1 so nasce depois que a tarefa i
     * terminou. */
    for (int i = 0; i < n; i++) {
        ultimo = exec_single(ts[i]);
    }

    return ultimo;
}

int exec_parallel(Task *ts[], int n) {
    pid_t pids[MAX_TAREFAS];

    /* PRIMEIRO laco: cria TODOS os filhos, sem esperar por nenhum.
     * O enunciado exige que todas as tarefas sejam iniciadas antes que o
     * ProcessFlow espere pelo termino do grupo — e a separacao em dois
     * lacos e exatamente o que garante isso. Juntar os dois lacos em um so
     * transformaria este metodo em sequencial. */
    for (int i = 0; i < n; i++) {
        int fd_in, fd_out;

        if (abrir_redirecionamentos(ts[i], &fd_in, &fd_out) != 0) {
            pids[i] = -1;   /* erro ja impresso; esta tarefa nao roda */
            continue;
        }

        pids[i] = task_spawn(ts[i], fd_in, fd_out, NULL, 0);

        fechar_se_aberto(fd_in);
        fechar_se_aberto(fd_out);
    }

    /* SEGUNDO laco: agora sim o pai recolhe cada filho.
     *
     * A coleta acontece na ordem de criacao, e nao na ordem de termino, e
     * isso esta correto: se o segundo filho terminar antes do primeiro, o
     * waitpid do primeiro apenas espera mais um pouco, e o do segundo
     * retorna imediatamente. Nenhum filho deixa de ser coletado, entao nao
     * sobra processo zumbi. */
    for (int i = 0; i < n; i++) {
        if (pids[i] > 0) {
            task_collect(pids[i], ts[i]->nome);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Pipe                                                                */
/* ------------------------------------------------------------------ */

int exec_pipe(Task *ts[], int n) {
    pid_t pids[MAX_TAREFAS];
    int   fd_entrada_arquivo = -1;   /* input da PRIMEIRA tarefa, se houver */
    int   fd_saida_arquivo   = -1;   /* output da ULTIMA tarefa, se houver  */

    if (n < 2) {
        fprintf(stderr, "erro: 'run pipe' precisa de pelo menos duas tarefas\n");
        return -1;
    }

    /* Numa cadeia de pipe, o stdin de cada tarefa vem da tarefa anterior e
     * o stdout vai para a proxima. Sobram duas pontas livres: a entrada da
     * primeira e a saida da ultima. Sao essas — e so essas — que podem vir
     * de arquivo. */
    if (ts[0]->arquivo_entrada[0] != '\0') {
        fd_entrada_arquivo = open(ts[0]->arquivo_entrada, O_RDONLY);
        if (fd_entrada_arquivo < 0) {
            fprintf(stderr, "erro: nao foi possivel abrir '%s' para leitura (tarefa '%s'): %s\n",
                    ts[0]->arquivo_entrada, ts[0]->nome, strerror(errno));
            return -1;
        }
    }

    if (ts[n - 1]->arquivo_saida[0] != '\0') {
        int flags = O_WRONLY | O_CREAT | (ts[n - 1]->saida_append ? O_APPEND : O_TRUNC);

        fd_saida_arquivo = open(ts[n - 1]->arquivo_saida, flags, 0644);
        if (fd_saida_arquivo < 0) {
            fprintf(stderr, "erro: nao foi possivel abrir '%s' para escrita (tarefa '%s'): %s\n",
                    ts[n - 1]->arquivo_saida, ts[n - 1]->nome, strerror(errno));
            fechar_se_aberto(fd_entrada_arquivo);
            return -1;
        }
    }

    /* fd_leitura_anterior guarda a ponta de LEITURA do pipe criado na
     * volta anterior do laco: e por ela que a tarefa atual recebe a saida
     * da tarefa anterior. Comeca em -1 porque a primeira tarefa nao tem
     * antecessora. */
    int fd_leitura_anterior = -1;
    int criados = 0;

    for (int i = 0; i < n; i++) {
        int fd[2] = { -1, -1 };
        int entrada;
        int saida;

        /* Cria um pipe entre a tarefa atual e a proxima. A ultima tarefa
         * da cadeia nao precisa de pipe: a saida dela vai para o arquivo
         * (se houver) ou para o stdout do ProcessFlow. */
        if (i < n - 1) {
            if (pipe(fd) < 0) {
                fprintf(stderr, "erro: nao foi possivel criar o pipe: %s\n", strerror(errno));
                break;
            }
        }

        /* Entrada: do pipe anterior; se for a primeira tarefa, do arquivo
         * de input, se houver; senao herda o stdin do ProcessFlow (-1). */
        if (i == 0) {
            entrada = fd_entrada_arquivo;
        } else {
            entrada = fd_leitura_anterior;
        }

        /* Saida: para o pipe novo; se for a ultima tarefa, para o arquivo
         * de output, se houver; senao herda o stdout do ProcessFlow (-1). */
        if (i < n - 1) {
            saida = fd[1];
        } else {
            saida = fd_saida_arquivo;
        }

        /* O filho precisa fechar a ponta de LEITURA do pipe que ele acabou
         * de herdar: ela pertence ao proximo elo da cadeia, nao a ele.
         * Deixar aberta faria o proximo processo nunca ver EOF caso este
         * aqui morresse antes. */
        int fechar[1];
        int n_fechar = 0;
        if (i < n - 1) {
            fechar[n_fechar++] = fd[0];
        }

        pids[i] = task_spawn(ts[i], entrada, saida, fechar, n_fechar);
        criados++;

        /* ---- Fechamento no PAI: a parte que decide se trava ou nao ----
         *
         * O pai criou os pipes, mas nao le nem escreve neles. Toda ponta
         * que ele deixar aberta continua contando como "escritor vivo" ou
         * "leitor vivo" para o kernel, e o processo do outro lado espera
         * para sempre por um EOF que nao chega.
         *
         * Por isso: assim que o filho e criado (e ja tem sua propria copia
         * dos descritores), o pai fecha os dele. */
        fechar_se_aberto(fd_leitura_anterior);

        if (i < n - 1) {
            close(fd[1]);                    /* quem escreve e o filho, nao o pai */
            fd_leitura_anterior = fd[0];     /* guarda a leitura para o proximo */
        } else {
            fd_leitura_anterior = -1;
        }
    }

    /* Fecha o que sobrou: a ultima ponta de leitura (se o laco terminou por
     * erro) e os arquivos de redirecionamento das extremidades. */
    fechar_se_aberto(fd_leitura_anterior);
    fechar_se_aberto(fd_entrada_arquivo);
    fechar_se_aberto(fd_saida_arquivo);

    /* So agora o pai espera. Todos os processos da cadeia foram criados e
     * estao rodando ao mesmo tempo — e assim que um pipe funciona: os
     * dados fluem enquanto os processos executam, sem arquivo temporario. */
    for (int i = 0; i < criados; i++) {
        if (pids[i] > 0) {
            task_collect(pids[i], ts[i]->nome);
        }
    }

    return 0;
}
