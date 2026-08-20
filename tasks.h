#ifndef TASKS_H
#define TASKS_H

#define MAX_TAREFAS 64  /* quantas tarefas podem estar cadastradas ao mesmo tempo */
#define MAX_ARGS    64  /* quantos argumentos uma tarefa pode ter (incluindo o programa) */
#define MAX_NOME    64
#define MAX_CAMINHO 256

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

    /* Redirecionamento configurado por input/output/append.
     * Caminho vazio ("") significa "sem redirecionamento": a tarefa herda
     * o stdin/stdout do ProcessFlow. */
    char  arquivo_entrada[MAX_CAMINHO];
    char  arquivo_saida[MAX_CAMINHO];
    int   saida_append;   /* 0 = truncar o arquivo (output), 1 = anexar (append) */
} Task;

/* Zera a tabela de tarefas. Chamada uma vez no inicio do main. */
void tasks_init(void);

/* Cadastra (ou recadastra, se o nome ja existir) uma tarefa.
 * argv[0] deve ser o programa. Retorna 0 em sucesso, -1 em erro
 * (ja imprimindo a mensagem correspondente). */
int task_register(const char *nome, char **argv, int argc);

/* Procura uma tarefa pelo nome. Retorna NULL se nao existir. */
Task *task_find(const char *nome);

/* Configura o arquivo de entrada da tarefa (comando input).
 * Confere na hora se o arquivo pode ser aberto para leitura; se nao puder,
 * imprime o erro e NAO grava a configuracao. Retorna 0 em sucesso. */
int task_set_input(Task *t, const char *arquivo);

/* Configura o arquivo de saida da tarefa.
 * append == 0 -> comando output (trunca o arquivo)
 * append == 1 -> comando append (escreve no final)
 * Confere na hora se o arquivo pode ser aberto para escrita. Retorna 0 em sucesso. */
int task_set_output(Task *t, const char *arquivo, int append);

#endif
