#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "tasks.h"

/* Tabela de tarefas. E "static" para ficar visivel so dentro deste arquivo:
 * quem quiser mexer nela precisa passar pelas funcoes declaradas em tasks.h. */
static Task tarefas[MAX_TAREFAS];
static int  total = 0;

void tasks_init(void) {
    total = 0;
}

Task *task_find(const char *nome) {
    for (int i = 0; i < total; i++) {
        if (strcmp(tarefas[i].nome, nome) == 0) {
            return &tarefas[i];
        }
    }
    return NULL;
}

/* Libera as strings do argv de uma tarefa. Usado quando o usuario
 * cadastra de novo uma tarefa com um nome que ja existia. */
static void liberar_argv(Task *t) {
    for (int i = 0; i < t->argc; i++) {
        free(t->argv[i]);
        t->argv[i] = NULL;
    }
    t->argc = 0;
}

int task_register(const char *nome, char **argv, int argc) {
    if (argc <= 0) {
        fprintf(stderr, "erro: a tarefa '%s' precisa de um programa\n", nome);
        return -1;
    }

    if (strlen(nome) >= MAX_NOME) {
        fprintf(stderr, "erro: nome de tarefa muito longo (maximo %d caracteres)\n",
                MAX_NOME - 1);
        return -1;
    }

    if (argc >= MAX_ARGS) {
        fprintf(stderr, "erro: a tarefa '%s' tem argumentos demais (maximo %d)\n",
                nome, MAX_ARGS - 1);
        return -1;
    }

    /* Se o nome ja existe, sobrescreve; senao, usa a proxima posicao livre. */
    Task *t = task_find(nome);
    int   nova = (t == NULL);

    if (nova) {
        if (total >= MAX_TAREFAS) {
            fprintf(stderr, "erro: limite de %d tarefas cadastradas atingido\n", MAX_TAREFAS);
            return -1;
        }
        t = &tarefas[total];
        t->argc = 0;
    } else {
        liberar_argv(t);  /* descarta o argv antigo antes de montar o novo */
    }

    strncpy(t->nome, nome, MAX_NOME - 1);
    t->nome[MAX_NOME - 1] = '\0';

    /* Recadastrar uma tarefa zera os redirecionamentos: o comando task
     * redefine a tarefa por completo, entao a configuracao anterior de
     * input/output nao vale mais para o programa novo. */
    t->arquivo_entrada[0] = '\0';
    t->arquivo_saida[0]   = '\0';
    t->saida_append       = 0;

    /* strdup copia cada argumento para memoria propria da tarefa. Isso e
     * necessario porque os tokens apontam para o buffer da linha lida, que
     * sera sobrescrito assim que o usuario digitar o proximo comando. */
    for (int i = 0; i < argc; i++) {
        t->argv[i] = strdup(argv[i]);
        if (t->argv[i] == NULL) {
            fprintf(stderr, "erro: memoria insuficiente ao cadastrar '%s'\n", nome);
            t->argc = i;
            liberar_argv(t);
            return -1;
        }
    }
    t->argv[argc] = NULL;  /* o execvp exige o NULL no fim do vetor */
    t->argc = argc;

    if (nova) {
        total++;
    }

    return 0;
}

int task_set_input(Task *t, const char *arquivo) {
    if (strlen(arquivo) >= MAX_CAMINHO) {
        fprintf(stderr, "erro: caminho de arquivo muito longo (maximo %d caracteres)\n",
                MAX_CAMINHO - 1);
        return -1;
    }

    /* Confere agora se o arquivo existe e pode ser lido, em vez de deixar
     * o problema aparecer so na hora do run. O descritor e fechado em
     * seguida: quem abre para valer e o executor, no momento da execucao. */
    int fd = open(arquivo, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "erro: nao foi possivel abrir '%s' para leitura: %s\n",
                arquivo, strerror(errno));
        return -1;
    }
    close(fd);

    strncpy(t->arquivo_entrada, arquivo, MAX_CAMINHO - 1);
    t->arquivo_entrada[MAX_CAMINHO - 1] = '\0';
    return 0;
}

int task_set_output(Task *t, const char *arquivo, int append) {
    if (strlen(arquivo) >= MAX_CAMINHO) {
        fprintf(stderr, "erro: caminho de arquivo muito longo (maximo %d caracteres)\n",
                MAX_CAMINHO - 1);
        return -1;
    }

    /* Mesma validacao antecipada da entrada. Aqui e importante usar as
     * MESMAS flags que o executor vai usar depois: validar com O_TRUNC e
     * depois abrir com O_APPEND (ou o contrario) poderia aceitar um caso
     * que falharia na execucao. */
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);

    int fd = open(arquivo, flags, 0644);
    if (fd < 0) {
        fprintf(stderr, "erro: nao foi possivel abrir '%s' para escrita: %s\n",
                arquivo, strerror(errno));
        return -1;
    }
    close(fd);

    strncpy(t->arquivo_saida, arquivo, MAX_CAMINHO - 1);
    t->arquivo_saida[MAX_CAMINHO - 1] = '\0';
    t->saida_append = append;
    return 0;
}
