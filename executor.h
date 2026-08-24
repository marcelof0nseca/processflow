#ifndef EXECUTOR_H
#define EXECUTOR_H

#include <sys/types.h>
#include "tasks.h"

#define MAX_JOBS 64

pid_t task_spawn(const Task *t, int fd_entrada, int fd_saida,
                 const int *fechar_no_filho, int n_fechar);

int task_collect(pid_t pid, const char *nome);

int exec_single(const Task *t);
int exec_sequential(Task *ts[], int n);
int exec_parallel(Task *ts[], int n);
int exec_pipe(Task *ts[], int n);

int exec_set_workdir(const char *diretorio);

int job_start(const Task *t);

void jobs_list(void);

int job_wait(int id);

void jobs_collect_all(void);

#endif
