#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "tasks.h"
#include "executor.h"

#define TAM_LINHA 1024

/* Quebra a linha em palavras e manda executar. Separado em funcao porque os
 * dois modos (interativo e workflow) fazem exatamente a mesma coisa aqui. */
static void processar_linha(char *linha) {
    char *tokens[MAX_TOKENS];
    int n = tokenize(linha, tokens);
    dispatch_command(n, tokens);
}

/* Modo interativo: mostra o prompt e le do teclado ate o "exit" ou ate EOF. */
static void modo_interativo(void) {
    char linha[TAM_LINHA];

    while (!should_exit()) {
        printf("processflow> ");
        fflush(stdout);  /* o prompt nao tem \n, entao precisa forcar a exibicao */

        if (fgets(linha, sizeof(linha), stdin) == NULL) {
            /* fgets devolve NULL no fim da entrada (Ctrl+D no terminal).
             * Isso e saida normal, nao erro: sai limpo, como se fosse "exit". */
            printf("\n");
            break;
        }

        processar_linha(linha);
    }
}

/* Modo workflow: le os comandos de um arquivo .pf.
 * O enunciado exige duas coisas aqui: nao mostrar o prompt, e imprimir cada
 * linha ANTES de processa-la. */
static void modo_workflow(const char *caminho) {
    FILE *arquivo = fopen(caminho, "r");

    if (arquivo == NULL) {
        /* Erro fatal do enunciado: arquivo de workflow nao pode ser aberto. */
        fprintf(stderr, "processflow: nao foi possivel abrir o arquivo de workflow '%s'\n",
                caminho);
        exit(EXIT_FAILURE);
    }

    char linha[TAM_LINHA];

    while (!should_exit() && fgets(linha, sizeof(linha), arquivo) != NULL) {
        /* Eco da linha lida. O fgets mantem o \n do arquivo, mas a ultima
         * linha pode nao ter, entao completa quando faltar. */
        printf("%s", linha);
        size_t tam = strlen(linha);
        if (tam == 0 || linha[tam - 1] != '\n') {
            printf("\n");
        }

        /* Cuidado: o tokenize modifica a linha, por isso o eco vem antes. */
        processar_linha(linha);
    }

    fclose(arquivo);
}

int main(int argc, char *argv[]) {
    /* Uso: ./processflow [workflowFile]
     * Sem argumento -> modo interativo. Com um argumento -> modo workflow.
     * Mais de um argumento e erro fatal segundo o enunciado. */
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

    /* Antes de encerrar, espera os jobs que ainda estiverem em background.
     * O enunciado diz que o ProcessFlow e responsavel por coletar o termino
     * dos processos filhos que criar — sair sem isso deixaria processos
     * orfaos, adotados pelo init, e o status deles nunca seria lido. */
    jobs_collect_all();

    return EXIT_SUCCESS;
}
