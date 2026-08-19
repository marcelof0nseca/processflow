#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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
 * Com -1 nos dois parametros nao faz nada, que e o caso do Dia 2.
 * A partir do Dia 3 e aqui que o pipe e o input/output se conectam. */
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

/* ------------------------------------------------------------------ */
/* Criacao de processo: fork + exec                                    */
/* ------------------------------------------------------------------ */

pid_t task_spawn(const Task *t, int fd_entrada, int fd_saida) {
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
         * mesmo teclado. E _exit() e nao exit(): o exit() executaria os
         * handlers e daria flush no buffer herdado do pai, duplicando
         * saida. O 127 e a convencao de shell para "comando nao encontrado". */
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
    pid_t pid = task_spawn(t, -1, -1);
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
        pids[i] = task_spawn(ts[i], -1, -1);
    }

    /* SEGUNDO laco: agora sim o pai recolhe cada filho.
     *
     * A coleta acontece na ordem de criacao, e nao na ordem de termino, e
     * isso esta correto: se o segundo filho terminar antes do primeiro, o
     * waitpid do primeiro apenas espera mais um pouco, e o do segundo
     * retorna imediatamente. Nenhum filho deixa de ser coletado, entao nao
     * sobra processo zumbi.
     *
     * O teste pids[i] > 0 evita chamar waitpid com um PID invalido caso
     * algum fork tenha falhado no laco anterior. */
    for (int i = 0; i < n; i++) {
        if (pids[i] > 0) {
            task_collect(pids[i], ts[i]->nome);
        }
    }

    return 0;
}
