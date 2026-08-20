#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sys/types.h>
#include "tasks.h"

/* Cria UM processo filho para executar a tarefa.
 *
 * Este e o unico ponto do programa que chama fork() + execvp(). Todos os
 * modos de execucao passam por aqui.
 *
 *   fd_entrada == -1  -> o filho usa o mesmo stdin do ProcessFlow
 *   fd_saida   == -1  -> o filho usa o mesmo stdout do ProcessFlow
 *
 * Qualquer outro valor e um descritor ja aberto (arquivo ou ponta de pipe)
 * que sera ligado ao stdin/stdout do filho com dup2() antes do exec.
 *
 * fechar_no_filho / n_fechar: descritores que o filho deve fechar antes do
 * exec por nao usa-los. Isso e necessario no pipe, onde cada filho herda
 * pontas que pertencem a outros elos da cadeia — uma ponta de escrita
 * esquecida aberta impede o EOF de chegar e trava a execucao.
 * Passe NULL e 0 quando nao houver nada extra a fechar.
 *
 * Retorna, NO PAI, o PID do filho criado (ou -1 se o fork falhou).
 * O filho nunca retorna desta funcao: ou ele vira o programa da tarefa,
 * ou ele morre com _exit(). */
pid_t task_spawn(const Task *t, int fd_entrada, int fd_saida,
                 const int *fechar_no_filho, int n_fechar);

/* Espera UM processo especifico terminar e reporta terminacao anormal.
 * Usa sempre o PID exato, nunca waitpid(-1), para nao coletar por engano
 * um filho que pertence a outro comando.
 * Retorna o codigo de saida da tarefa, ou -1 se o waitpid falhou. */
int task_collect(pid_t pid, const char *nome);

/* run <nome> — cria o filho e espera ele terminar. */
int exec_single(const Task *t);

/* run sequential t1 t2 t3 — cada tarefa so comeca depois que a anterior
 * terminou. */
int exec_sequential(Task *ts[], int n);

/* run parallel t1 t2 t3 — TODAS as tarefas sao criadas antes de o
 * ProcessFlow esperar por qualquer uma delas. */
int exec_parallel(Task *ts[], int n);

/* run pipe t1 t2 t3 — a saida de cada tarefa vira a entrada da seguinte.
 * Cria n-1 pipes e n processos, todos vivos ao mesmo tempo.
 * O redirecionamento por arquivo da primeira tarefa (input) e da ultima
 * (output/append) e respeitado; nas intermediarias o pipe tem prioridade. */
int exec_pipe(Task *ts[], int n);

#endif
