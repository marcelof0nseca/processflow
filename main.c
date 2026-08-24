#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "parser.h"
#include "tasks.h"
#include "executor.h"

#define TAM_LINHA 1024

static void processar_linha(char *linha) {
    char *tokens[MAX_TOKENS];
    int n = tokenize(linha, tokens);
    dispatch_command(n, tokens);
}

static void modo_interativo(void) {
    char linha[TAM_LINHA];


    set_modo_interativo(isatty(STDIN_FILENO));

    while (!should_exit()) {
        if (modo_interativo_ativo()) {
            printf("processflow> ");
            fflush(stdout);
        }

        if (fgets(linha, sizeof(linha), stdin) == NULL) {

            if (modo_interativo_ativo()) {
                printf("\n");
            }
            break;
        }

        processar_linha(linha);
    }
}

static void modo_workflow(const char *caminho) {

    set_modo_interativo(0);

    FILE *arquivo = fopen(caminho, "r");

    if (arquivo == NULL) {

        fprintf(stderr, "processflow: nao foi possivel abrir o arquivo de workflow '%s'\n",
                caminho);
        exit(EXIT_FAILURE);
    }

    char linha[TAM_LINHA];

    while (!should_exit() && fgets(linha, sizeof(linha), arquivo) != NULL) {

        printf("%s", linha);
        size_t tam = strlen(linha);
        if (tam == 0 || linha[tam - 1] != '\n') {
            printf("\n");
        }


        processar_linha(linha);
    }

    fclose(arquivo);
}

int main(int argc, char *argv[]) {

    if (argc > 2) {
        fprintf(stderr, "processflow: numero incorreto de argumentos\n");
        fprintf(stderr, "uso: %s [workflowFile]\n", argv[0]);
        return EXIT_FAILURE;
    }

    tasks_init();

    if (argc == 2) {
        modo_workflow(argv[1]);
    } else {
        modo_interativo();
    }


    jobs_collect_all();

    return EXIT_SUCCESS;
}
