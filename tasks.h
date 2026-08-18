#ifndef TASKS_H
#define TASKS_H

#define MAX_TAREFAS 64  /* quantas tarefas podem estar cadastradas ao mesmo tempo */
#define MAX_ARGS    64  /* quantos argumentos uma tarefa pode ter (incluindo o programa) */
#define MAX_NOME    64

/* Uma tarefa cadastrada pelo comando:
 *     task <nome> <programa> [argumentos...]
 *
 * O vetor argv ja fica montado no formato exigido pelo execvp():
 *     argv[0] = caminho do programa
 *     argv[1..n-1] = argumentos
 *     argv[n] = NULL   <- o execvp precisa desse NULL no final
 *
 * Por isso nao existe um campo "programa" separado: ele e o proprio argv[0]. */
typedef struct {
    char  nome[MAX_NOME];
    char *argv[MAX_ARGS];
    int   argc;
} Task;

/* Zera a tabela de tarefas. Chamada uma vez no inicio do main. */
void tasks_init(void);

/* Cadastra (ou recadastra, se o nome ja existir) uma tarefa.
 * argv[0] deve ser o programa. Retorna 0 em sucesso, -1 em erro
 * (ja imprimindo a mensagem correspondente). */
int task_register(const char *nome, char **argv, int argc);

/* Procura uma tarefa pelo nome. Retorna NULL se nao existir. */
Task *task_find(const char *nome);

#endif
