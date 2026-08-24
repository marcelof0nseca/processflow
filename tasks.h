#ifndef TASKS_H
#define TASKS_H

#define MAX_TAREFAS 64
#define MAX_ARGS    64
#define MAX_NOME    64
#define MAX_CAMINHO 256

typedef struct {
    char  nome[MAX_NOME];
    char *argv[MAX_ARGS];
    int   argc;


    char  arquivo_entrada[MAX_CAMINHO];
    char  arquivo_saida[MAX_CAMINHO];
    int   saida_append;
} Task;

void tasks_init(void);

int task_register(const char *nome, char **argv, int argc);

Task *task_find(const char *nome);

int task_set_input(Task *t, const char *arquivo);

int task_set_output(Task *t, const char *arquivo, int append);

#endif
