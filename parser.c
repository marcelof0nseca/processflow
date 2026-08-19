#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "tasks.h"
#include "executor.h"

/* Sinalizador do comando "exit". Fica aqui (e nao no main.c) porque quem
 * processa o comando e este arquivo; o main so consulta via should_exit(). */
static int sair = 0;

int should_exit(void) {
    return sair;
}

int tokenize(char *linha, char *tokens[]) {
    int n = 0;

    /* O strtok trata sequencias de separadores como UM separador so, entao
     * "task     listar    /bin/ls" ja funciona sem nenhum tratamento extra.
     * Incluir \r na lista cobre arquivos .pf salvos no Windows (CRLF). */
    char *tok = strtok(linha, " \t\r\n");

    while (tok != NULL && n < MAX_TOKENS - 1) {
        tokens[n++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }

    tokens[n] = NULL;
    return n;
}

/* task <nome> <programa> [argumentos...]
 * Precisa de pelo menos 3 palavras: "task", o nome e o programa. */
static void cmd_task(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "erro: uso correto: task <nome> <programa> [argumentos...]\n");
        return;
    }

    /* &argv[2] e o endereco da terceira palavra: dali em diante e o programa
     * e seus argumentos, que e exatamente o que a tarefa precisa guardar. */
    if (task_register(argv[1], &argv[2], argc - 2) == 0) {
        printf("tarefa '%s' cadastrada\n", argv[1]);
    }
}

/* Converte uma lista de NOMES de tarefa em ponteiros para as Tasks.
 * Se qualquer nome nao existir, imprime o erro e devolve -1: o comando run
 * inteiro e cancelado, mas o ProcessFlow continua lendo comandos.
 *
 * Cancelar o grupo todo (em vez de pular a tarefa faltante) e uma decisao
 * consciente: numa cadeia de pipe, executar so parte das tarefas produziria
 * um resultado silenciosamente errado. */
static int resolver_tarefas(char *nomes[], int n, Task *saida[]) {
    for (int i = 0; i < n; i++) {
        Task *t = task_find(nomes[i]);
        if (t == NULL) {
            fprintf(stderr, "erro: tarefa '%s' nao foi cadastrada\n", nomes[i]);
            return -1;
        }
        saida[i] = t;
    }
    return 0;
}

/* run <nome>
 * run sequential <t1> <t2> ...
 * run parallel   <t1> <t2> ...
 * run pipe       <t1> <t2> ...   (Dia 3) */
static void cmd_run(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "erro: uso correto: run <nome> | "
                        "run <sequential|parallel|pipe> <tarefas...>\n");
        return;
    }

    const char *modo = argv[1];
    int em_grupo = strcmp(modo, "sequential") == 0 ||
                   strcmp(modo, "parallel")   == 0 ||
                   strcmp(modo, "pipe")       == 0;

    /* Caso simples: "run <nome>" */
    if (!em_grupo) {
        if (argc > 2) {
            fprintf(stderr, "erro: 'run <nome>' aceita uma unica tarefa; "
                            "use sequential, parallel ou pipe para grupos\n");
            return;
        }

        Task *t = task_find(argv[1]);
        if (t == NULL) {
            fprintf(stderr, "erro: tarefa '%s' nao foi cadastrada\n", argv[1]);
            return;
        }

        exec_single(t);
        return;
    }

    /* Caso em grupo: as tarefas comecam em argv[2] */
    int n = argc - 2;

    if (n < 1) {
        fprintf(stderr, "erro: 'run %s' precisa de pelo menos uma tarefa\n", modo);
        return;
    }
    if (n > MAX_TAREFAS) {
        fprintf(stderr, "erro: no maximo %d tarefas por comando run\n", MAX_TAREFAS);
        return;
    }

    Task *ts[MAX_TAREFAS];
    if (resolver_tarefas(&argv[2], n, ts) != 0) {
        return;  /* alguma tarefa nao existe: erro ja foi impresso */
    }

    if (strcmp(modo, "sequential") == 0) {
        exec_sequential(ts, n);
    } else if (strcmp(modo, "parallel") == 0) {
        exec_parallel(ts, n);
    } else {
        fprintf(stderr, "erro: 'run pipe' ainda nao implementado (Dia 3)\n");
    }
}

void dispatch_command(int argc, char *argv[]) {
    /* Linha vazia ou so com espacos: nao e erro, simplesmente nao faz nada. */
    if (argc == 0) {
        return;
    }

    if (strcmp(argv[0], "exit") == 0) {
        sair = 1;
        return;
    }

    if (strcmp(argv[0], "task") == 0) {
        cmd_task(argc, argv);
        return;
    }

    if (strcmp(argv[0], "run") == 0) {
        cmd_run(argc, argv);
        return;
    }

    /* Os comandos input, output, append, workdir, start, jobs e wait
     * entram nos proximos dias. Ate la caem aqui como desconhecidos. */
    fprintf(stderr, "erro: comando desconhecido '%s'\n", argv[0]);
}
