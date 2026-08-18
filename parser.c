#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "tasks.h"

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

    /* Os comandos run, input, output, append, workdir, start, jobs e wait
     * entram nos proximos dias. Ate la caem aqui como desconhecidos. */
    fprintf(stderr, "erro: comando desconhecido '%s'\n", argv[0]);
}
