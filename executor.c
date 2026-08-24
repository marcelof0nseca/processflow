#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "executor.h"
#include "parser.h"

static char diretorio_trabalho[MAX_CAMINHO] = "";

typedef struct {
    int   id;
    pid_t pid;
    char  nome[MAX_NOME];
    int   ativo;
    int   codigo;
} Job;

static Job jobs[MAX_JOBS];
static int total_jobs = 0;
static int proximo_id  = 1;

static int codigo_de_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {

        return 128 + WTERMSIG(status);
    }
    return -1;
}

static int reportar_status(const char *nome, pid_t pid, int status) {
    int codigo = codigo_de_status(status);

    if (WIFEXITED(status) && codigo != 0) {
        fprintf(stderr, "aviso: tarefa '%s' (pid %d) terminou com codigo %d\n",
                nome, (int)pid, codigo);
    } else if (WIFSIGNALED(status)) {
        fprintf(stderr, "aviso: tarefa '%s' (pid %d) terminou por sinal %d\n",
                nome, (int)pid, WTERMSIG(status));
    }

    return codigo;
}

static void aplicar_redirecionamentos(int fd_entrada, int fd_saida) {
    if (fd_entrada != -1) {
        if (dup2(fd_entrada, STDIN_FILENO) < 0) {
            fprintf(stderr, "erro: dup2 da entrada falhou: %s\n", strerror(errno));
            _exit(126);
        }
        if (fd_entrada != STDIN_FILENO) {
            close(fd_entrada);
        }
    }

    if (fd_saida != -1) {
        if (dup2(fd_saida, STDOUT_FILENO) < 0) {
            fprintf(stderr, "erro: dup2 da saida falhou: %s\n", strerror(errno));
            _exit(126);
        }
        if (fd_saida != STDOUT_FILENO) {
            close(fd_saida);
        }
    }
}

static int abrir_redirecionamentos(const Task *t, int *fd_in, int *fd_out) {
    *fd_in  = -1;
    *fd_out = -1;

    if (t->arquivo_entrada[0] != '\0') {
        *fd_in = open(t->arquivo_entrada, O_RDONLY);
        if (*fd_in < 0) {
            fprintf(stderr, "erro: nao foi possivel abrir '%s' para leitura (tarefa '%s'): %s\n",
                    t->arquivo_entrada, t->nome, strerror(errno));
            return -1;
        }
    }

    if (t->arquivo_saida[0] != '\0') {
        int flags = O_WRONLY | O_CREAT | (t->saida_append ? O_APPEND : O_TRUNC);

        *fd_out = open(t->arquivo_saida, flags, 0644);
        if (*fd_out < 0) {
            fprintf(stderr, "erro: nao foi possivel abrir '%s' para escrita (tarefa '%s'): %s\n",
                    t->arquivo_saida, t->nome, strerror(errno));
            if (*fd_in != -1) {
                close(*fd_in);
                *fd_in = -1;
            }
            return -1;
        }
    }

    return 0;
}

static void fechar_se_aberto(int fd) {
    if (fd != -1) {
        close(fd);
    }
}

static pid_t iniciar_tarefa(const Task *t) {
    int fd_in, fd_out;

    if (abrir_redirecionamentos(t, &fd_in, &fd_out) != 0) {
        return -1;
    }

    pid_t pid = task_spawn(t, fd_in, fd_out, NULL, 0);

    fechar_se_aberto(fd_in);
    fechar_se_aberto(fd_out);

    return pid;
}

int exec_set_workdir(const char *diretorio) {
    struct stat info;

    if (strlen(diretorio) >= MAX_CAMINHO) {
        fprintf(stderr, "erro: caminho muito longo (maximo %d caracteres)\n",
                MAX_CAMINHO - 1);
        return -1;
    }


    if (stat(diretorio, &info) != 0) {
        fprintf(stderr, "erro: diretorio '%s' nao encontrado: %s\n",
                diretorio, strerror(errno));
        return -1;
    }

    if (!S_ISDIR(info.st_mode)) {
        fprintf(stderr, "erro: '%s' nao e um diretorio\n", diretorio);
        return -1;
    }

    strncpy(diretorio_trabalho, diretorio, MAX_CAMINHO - 1);
    diretorio_trabalho[MAX_CAMINHO - 1] = '\0';
    return 0;
}

pid_t task_spawn(const Task *t, int fd_entrada, int fd_saida,
                 const int *fechar_no_filho, int n_fechar) {

    fflush(NULL);

    pid_t pid = fork();


    if (pid < 0) {
        fprintf(stderr, "erro: nao foi possivel criar processo para a tarefa '%s': %s\n",
                t->nome, strerror(errno));
        return -1;
    }


    if (pid == 0) {
        aplicar_redirecionamentos(fd_entrada, fd_saida);


        for (int i = 0; i < n_fechar; i++) {
            if (fechar_no_filho[i] >= 0) {
                close(fechar_no_filho[i]);
            }
        }


        if (diretorio_trabalho[0] != '\0') {
            if (chdir(diretorio_trabalho) != 0) {
                fprintf(stderr, "erro: nao foi possivel entrar em '%s' (tarefa '%s'): %s\n",
                        diretorio_trabalho, t->nome, strerror(errno));
                _exit(126);
            }
        }


        execvp(t->argv[0], t->argv);


        fprintf(stderr, "erro: nao foi possivel executar '%s' (tarefa '%s'): %s\n",
                t->argv[0], t->nome, strerror(errno));


        _exit(127);
    }


    return pid;
}

int task_collect(pid_t pid, const char *nome) {
    int status;


    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "erro: waitpid do pid %d falhou: %s\n",
                (int)pid, strerror(errno));
        return -1;
    }

    return reportar_status(nome, pid, status);
}

int exec_single(const Task *t) {
    pid_t pid = iniciar_tarefa(t);

    if (pid < 0) {
        return -1;
    }
    return task_collect(pid, t->nome);
}

int exec_sequential(Task *ts[], int n) {
    int ultimo = 0;


    for (int i = 0; i < n; i++) {
        ultimo = exec_single(ts[i]);
    }

    return ultimo;
}

int exec_parallel(Task *ts[], int n) {
    pid_t pids[MAX_TAREFAS];


    for (int i = 0; i < n; i++) {
        pids[i] = iniciar_tarefa(ts[i]);
    }


    for (int i = 0; i < n; i++) {
        if (pids[i] > 0) {
            task_collect(pids[i], ts[i]->nome);
        }
    }

    return 0;
}

int exec_pipe(Task *ts[], int n) {
    pid_t pids[MAX_TAREFAS];
    int   fd_entrada_arquivo = -1;
    int   fd_saida_arquivo   = -1;

    if (n < 2) {
        fprintf(stderr, "erro: 'run pipe' precisa de pelo menos duas tarefas\n");
        return -1;
    }


    if (ts[0]->arquivo_entrada[0] != '\0') {
        fd_entrada_arquivo = open(ts[0]->arquivo_entrada, O_RDONLY);
        if (fd_entrada_arquivo < 0) {
            fprintf(stderr, "erro: nao foi possivel abrir '%s' para leitura (tarefa '%s'): %s\n",
                    ts[0]->arquivo_entrada, ts[0]->nome, strerror(errno));
            return -1;
        }
    }

    if (ts[n - 1]->arquivo_saida[0] != '\0') {
        int flags = O_WRONLY | O_CREAT | (ts[n - 1]->saida_append ? O_APPEND : O_TRUNC);

        fd_saida_arquivo = open(ts[n - 1]->arquivo_saida, flags, 0644);
        if (fd_saida_arquivo < 0) {
            fprintf(stderr, "erro: nao foi possivel abrir '%s' para escrita (tarefa '%s'): %s\n",
                    ts[n - 1]->arquivo_saida, ts[n - 1]->nome, strerror(errno));
            fechar_se_aberto(fd_entrada_arquivo);
            return -1;
        }
    }


    int fd_leitura_anterior = -1;
    int criados = 0;

    for (int i = 0; i < n; i++) {
        int fd[2] = { -1, -1 };
        int entrada;
        int saida;

        if (i < n - 1) {
            if (pipe(fd) < 0) {
                fprintf(stderr, "erro: nao foi possivel criar o pipe: %s\n", strerror(errno));
                break;
            }
        }

        entrada = (i == 0) ? fd_entrada_arquivo : fd_leitura_anterior;
        saida   = (i < n - 1) ? fd[1] : fd_saida_arquivo;


        int fechar[1];
        int n_fechar = 0;
        if (i < n - 1) {
            fechar[n_fechar++] = fd[0];
        }

        pids[i] = task_spawn(ts[i], entrada, saida, fechar, n_fechar);
        criados++;


        fechar_se_aberto(fd_leitura_anterior);

        if (i < n - 1) {
            close(fd[1]);
            fd_leitura_anterior = fd[0];
        } else {
            fd_leitura_anterior = -1;
        }
    }

    fechar_se_aberto(fd_leitura_anterior);
    fechar_se_aberto(fd_entrada_arquivo);
    fechar_se_aberto(fd_saida_arquivo);


    for (int i = 0; i < criados; i++) {
        if (pids[i] > 0) {
            task_collect(pids[i], ts[i]->nome);
        }
    }

    return 0;
}

static Job *job_find(int id) {
    for (int i = 0; i < total_jobs; i++) {
        if (jobs[i].id == id) {
            return &jobs[i];
        }
    }
    return NULL;
}

static void jobs_atualizar(void) {
    for (int i = 0; i < total_jobs; i++) {
        if (!jobs[i].ativo) {
            continue;
        }

        int   status;
        pid_t r = waitpid(jobs[i].pid, &status, WNOHANG);

        if (r == jobs[i].pid) {
            jobs[i].ativo  = 0;
            jobs[i].codigo = codigo_de_status(status);
        }

    }
}

int job_start(const Task *t) {
    if (total_jobs >= MAX_JOBS) {
        fprintf(stderr, "erro: limite de %d jobs atingido\n", MAX_JOBS);
        return -1;
    }

    pid_t pid = iniciar_tarefa(t);

    if (pid < 0) {
        return -1;
    }


    Job *j = &jobs[total_jobs];
    j->id     = proximo_id++;
    j->pid    = pid;
    j->ativo  = 1;
    j->codigo = 0;
    strncpy(j->nome, t->nome, MAX_NOME - 1);
    j->nome[MAX_NOME - 1] = '\0';

    total_jobs++;

    printf("[%d] %d\n", j->id, (int)pid);
    return j->id;
}

void jobs_list(void) {
    jobs_atualizar();

    if (total_jobs == 0) {
        printf("nenhum job iniciado\n");
        return;
    }

    for (int i = 0; i < total_jobs; i++) {
        if (jobs[i].ativo) {
            printf("[%d] %d  rodando     %s\n",
                   jobs[i].id, (int)jobs[i].pid, jobs[i].nome);
        } else {
            printf("[%d] %d  concluido   %s (codigo %d)\n",
                   jobs[i].id, (int)jobs[i].pid, jobs[i].nome, jobs[i].codigo);
        }
    }
}

int job_wait(int id) {
    Job *j = job_find(id);

    if (j == NULL) {
        fprintf(stderr, "erro: job %d nao existe\n", id);
        return -1;
    }

    if (!j->ativo) {
        if (modo_interativo_ativo()) {
            printf("job [%d] ja havia terminado com codigo %d\n", j->id, j->codigo);
        }
        return j->codigo;
    }


    int status;
    if (waitpid(j->pid, &status, 0) < 0) {
        fprintf(stderr, "erro: waitpid do job %d (pid %d) falhou: %s\n",
                j->id, (int)j->pid, strerror(errno));
        j->ativo = 0;
        return -1;
    }

    j->ativo  = 0;
    j->codigo = codigo_de_status(status);


    if (modo_interativo_ativo()) {
        printf("job [%d] (pid %d, tarefa '%s') terminou com codigo %d\n",
               j->id, (int)j->pid, j->nome, j->codigo);
    }

    return j->codigo;
}

void jobs_collect_all(void) {
    jobs_atualizar();

    for (int i = 0; i < total_jobs; i++) {
        if (!jobs[i].ativo) {
            continue;
        }

        if (modo_interativo_ativo()) {
            printf("aguardando job [%d] (pid %d, tarefa '%s')...\n",
                   jobs[i].id, (int)jobs[i].pid, jobs[i].nome);
        }

        int status;
        if (waitpid(jobs[i].pid, &status, 0) < 0) {
            fprintf(stderr, "erro: waitpid do job %d falhou: %s\n",
                    jobs[i].id, strerror(errno));
        } else {
            jobs[i].codigo = codigo_de_status(status);
        }
        jobs[i].ativo = 0;
    }
}
