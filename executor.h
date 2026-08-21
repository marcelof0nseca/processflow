#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sys/types.h>
#include "tasks.h"

#define MAX_JOBS 64

/* Cria UM processo filho para executar a tarefa.
 *
 * Este e o unico ponto do programa que chama fork() + execvp(). Todos os
 * modos de execucao passam por aqui, inclusive o background.
 *
 *   fd_entrada == -1  -> o filho usa o mesmo stdin do ProcessFlow
 *   fd_saida   == -1  -> o filho usa o mesmo stdout do ProcessFlow
 *
 * fechar_no_filho / n_fechar: descritores que o filho deve fechar antes do
 * exec por nao usa-los (usado pelo pipe). Passe NULL e 0 se nao houver.
 *
 * Retorna, NO PAI, o PID do filho criado (ou -1 se o fork falhou).
 * O filho nunca retorna desta funcao. */
pid_t task_spawn(const Task *t, int fd_entrada, int fd_saida,
                 const int *fechar_no_filho, int n_fechar);

/* Espera UM processo especifico terminar e reporta terminacao anormal.
 * Usa sempre o PID exato, nunca waitpid(-1).
 * Retorna o codigo de saida da tarefa, ou -1 se o waitpid falhou. */
int task_collect(pid_t pid, const char *nome);

/* ---------------- Modos de execucao ---------------- */

int exec_single(const Task *t);
int exec_sequential(Task *ts[], int n);
int exec_parallel(Task *ts[], int n);
int exec_pipe(Task *ts[], int n);

/* ---------------- Diretorio de trabalho ---------------- */

/* Comando workdir <diretorio>.
 * Confere que o caminho existe e e mesmo um diretorio; se nao for, imprime
 * o erro e nao altera nada. Retorna 0 em sucesso.
 *
 * O chdir() em si NAO acontece aqui: ele e aplicado dentro de cada processo
 * filho, logo antes do exec. Assim o ProcessFlow mantem o proprio diretorio,
 * e os caminhos relativos dele (arquivo .pf, arquivos de redirecionamento)
 * continuam valendo. */
int exec_set_workdir(const char *diretorio);

/* ---------------- Jobs em background ---------------- */

/* Comando start <tarefa>. Cria o processo e NAO espera por ele.
 * Registra o job na tabela e imprime "[jobId] PID".
 * Retorna o jobId, ou -1 em erro. */
int job_start(const Task *t);

/* Comando jobs. Atualiza o estado de cada job com waitpid(WNOHANG) — que
 * nao bloqueia — e lista todos. */
void jobs_list(void);

/* Comando wait <jobId>. Bloqueia ate aquele job terminar.
 * Retorna o codigo de saida, ou -1 se o jobId nao existir. */
int job_wait(int id);

/* Coleta todos os jobs ainda em execucao. Chamada antes de o ProcessFlow
 * encerrar, para nao deixar processo filho orfao nem zumbi. */
void jobs_collect_all(void);

#endif
