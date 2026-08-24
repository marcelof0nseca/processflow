#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "tasks.h"
#include "executor.h"

static int sair = 0;

static int interativo = 0;

int should_exit(void) {
    return sair;
}

void set_modo_interativo(int valor) {
    interativo = valor;
}

int modo_interativo_ativo(void) {
    return interativo;
}

int tokenize(char *linha, char *tokens[]) {
    int n = 0;


    char *tok = strtok(linha, " \t\r\n");

    while (tok != NULL && n < MAX_TOKENS - 1) {
        tokens[n++] = tok;
        tok = strtok(NULL, " \t\r\n");
    }

    tokens[n] = NULL;
    return n;
}

static void cmd_task(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "erro: uso correto: task <nome> <programa> [argumentos...]\n");
        return;
    }


    if (task_register(argv[1], &argv[2], argc - 2) == 0 && modo_interativo_ativo()) {
        printf("tarefa '%s' cadastrada\n", argv[1]);
    }
}

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
        return;
    }

    if (strcmp(modo, "sequential") == 0) {
        exec_sequential(ts, n);
    } else if (strcmp(modo, "parallel") == 0) {
        exec_parallel(ts, n);
    } else {
        exec_pipe(ts, n);
    }
}

static void cmd_redirecionar(int argc, char *argv[]) {
    const char *comando = argv[0];

    if (argc != 3) {
        fprintf(stderr, "erro: uso correto: %s <tarefa> <arquivo>\n", comando);
        return;
    }

    Task *t = task_find(argv[1]);
    if (t == NULL) {
        fprintf(stderr, "erro: tarefa '%s' nao foi cadastrada\n", argv[1]);
        return;
    }

    int entrada = (strcmp(comando, "input") == 0);
    int resultado;

    if (entrada) {
        resultado = task_set_input(t, argv[2]);
    } else {

        resultado = task_set_output(t, argv[2], strcmp(comando, "append") == 0);
    }

    if (resultado == 0 && modo_interativo_ativo()) {
        printf("tarefa '%s': %s redirecionada para '%s'\n",
               t->nome, entrada ? "entrada" : "saida", argv[2]);
    }
}

static void cmd_workdir(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "erro: uso correto: workdir <diretorio>\n");
        return;
    }

    if (exec_set_workdir(argv[1]) == 0 && modo_interativo_ativo()) {
        printf("diretorio de trabalho das tarefas: '%s'\n", argv[1]);
    }
}

static void cmd_start(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "erro: uso correto: start <tarefa>\n");
        return;
    }

    Task *t = task_find(argv[1]);
    if (t == NULL) {
        fprintf(stderr, "erro: tarefa '%s' nao foi cadastrada\n", argv[1]);
        return;
    }

    job_start(t);
}

static void cmd_wait(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "erro: uso correto: wait <jobId>\n");
        return;
    }


    char *fim;
    long  id = strtol(argv[1], &fim, 10);

    if (*fim != '\0' || fim == argv[1] || id <= 0) {
        fprintf(stderr, "erro: jobId invalido: '%s'\n", argv[1]);
        return;
    }

    job_wait((int)id);
}

void dispatch_command(int argc, char *argv[]) {

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

    if (strcmp(argv[0], "input")  == 0 ||
        strcmp(argv[0], "output") == 0 ||
        strcmp(argv[0], "append") == 0) {
        cmd_redirecionar(argc, argv);
        return;
    }

    if (strcmp(argv[0], "workdir") == 0) {
        cmd_workdir(argc, argv);
        return;
    }

    if (strcmp(argv[0], "start") == 0) {
        cmd_start(argc, argv);
        return;
    }

    if (strcmp(argv[0], "jobs") == 0) {
        if (argc != 1) {
            fprintf(stderr, "erro: uso correto: jobs\n");
            return;
        }
        jobs_list();
        return;
    }

    if (strcmp(argv[0], "wait") == 0) {
        cmd_wait(argc, argv);
        return;
    }

    fprintf(stderr, "erro: comando desconhecido '%s'\n", argv[0]);
}
