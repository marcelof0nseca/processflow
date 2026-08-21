#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "executor.h"
#include "parser.h"   /* apenas para consultar modo_interativo_ativo() */

/* ------------------------------------------------------------------ */
/* Estado do modulo                                                    */
/* ------------------------------------------------------------------ */

/* Diretorio configurado pelo comando workdir. Vazio significa "nenhum":
 * os filhos herdam o diretorio do proprio ProcessFlow. */
static char diretorio_trabalho[MAX_CAMINHO] = "";

/* Um job em background, criado pelo comando start. */
typedef struct {
    int   id;                 /* jobId sequencial, comeca em 1 */
    pid_t pid;
    char  nome[MAX_NOME];
    int   ativo;              /* 1 = ainda rodando, 0 = ja coletado */
    int   codigo;             /* codigo de saida, valido quando ativo == 0 */
} Job;

static Job jobs[MAX_JOBS];
static int total_jobs = 0;
static int proximo_id  = 1;

/* ------------------------------------------------------------------ */
/* Funcoes de apoio                                                    */
/* ------------------------------------------------------------------ */

/* Extrai o codigo de saida do inteiro devolvido pelo waitpid.
 *
 * O status NAO e o codigo de saida: e um inteiro com varios campos
 * empacotados. As macros WIFEXITED/WEXITSTATUS/WIFSIGNALED existem
 * para desempacotar isso sem depender do layout dos bits. */
static int codigo_de_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        /* Terminou por sinal (Ctrl+C, kill, segfault...) e nao por return.
         * A convencao de shell e somar 128 ao numero do sinal. */
        return 128 + WTERMSIG(status);
    }
    return -1;
}

/* Interpreta o status e avisa quando a terminacao foi anormal.
 * Saida normal com codigo 0 nao imprime nada, para nao poluir a saida do
 * programa que a tarefa executou. */
static int reportar_status(const char *nome, pid_t pid, int status) {
    if (WIFEXITED(status)) {
        int codigo = WEXITSTATUS(status);
        if (codigo != 0) {
            fprintf(stderr, "aviso: tarefa '%s' (pid %d) terminou com codigo %d\n",
                    nome, (int)pid, codigo);
        }
        return codigo;
    }

    if (WIFSIGNALED(status)) {
        fprintf(stderr, "aviso: tarefa '%s' (pid %d) terminou por sinal %d\n",
                nome, (int)pid, WTERMSIG(status));
        return 128 + WTERMSIG(status);
    }

    return -1;
}

/* Aplica os redirecionamentos no processo FILHO, antes do exec.
 * Com -1 nos dois parametros nao faz nada, e o filho herda os descritores
 * do ProcessFlow. */
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

/* Abre os arquivos configurados por input/output/append.
 *
 * A abertura acontece NO PAI, antes do fork. Assim, se o arquivo nao puder
 * ser aberto, a mensagem de erro sai de um lugar so e a execucao e
 * cancelada sem sequer criar o processo filho.
 *
 * Devolve -1 (com mensagem ja impressa) se algum arquivo falhar; nesse caso
 * fecha o que ja tinha aberto, para nao vazar descritor. */
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

/* Fecha um descritor se ele estiver aberto. Encurta o codigo em varios
 * pontos onde -1 significa "nao existe". */
static void fechar_se_aberto(int fd) {
    if (fd != -1) {
        close(fd);
    }
}

/* ------------------------------------------------------------------ */
/* Diretorio de trabalho                                               */
/* ------------------------------------------------------------------ */

int exec_set_workdir(const char *diretorio) {
    struct stat info;

    if (strlen(diretorio) >= MAX_CAMINHO) {
        fprintf(stderr, "erro: caminho muito longo (maximo %d caracteres)\n",
                MAX_CAMINHO - 1);
        return -1;
    }

    /* stat falha se o caminho nao existe; se existir, e preciso ainda
     * conferir que e um diretorio e nao um arquivo comum. */
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

/* ------------------------------------------------------------------ */
/* Criacao de processo: fork + exec                                    */
/* ------------------------------------------------------------------ */

pid_t task_spawn(const Task *t, int fd_entrada, int fd_saida,
                 const int *fechar_no_filho, int n_fechar) {
    /* Esvazia os buffers do stdio ANTES do fork.
     *
     * Motivo: o fork copia a memoria do processo, inclusive o buffer de
     * saida que ainda nao foi gravado. Se sobrar texto pendente no buffer,
     * pai e filho vao gravar esse mesmo texto, e ele aparece duplicado. */
    fflush(NULL);

    pid_t pid = fork();

    /* Caso 1: fork falhou. Nenhum filho foi criado. */
    if (pid < 0) {
        fprintf(stderr, "erro: nao foi possivel criar processo para a tarefa '%s': %s\n",
                t->nome, strerror(errno));
        return -1;
    }

    /* Caso 2: pid == 0 -> este codigo esta rodando NO FILHO. */
    if (pid == 0) {
        aplicar_redirecionamentos(fd_entrada, fd_saida);

        /* Fecha as pontas de pipe que pertencem a outros elos da cadeia.
         * Vem DEPOIS do dup2: os descritores que viraram stdin/stdout ja
         * foram copiados, entao fechar os originais aqui e seguro. */
        for (int i = 0; i < n_fechar; i++) {
            if (fechar_no_filho[i] >= 0) {
                close(fechar_no_filho[i]);
            }
        }

        /* O chdir acontece AQUI, no filho, e nao no ProcessFlow.
         *
         * Vem depois da abertura dos redirecionamentos (que foi feita no
         * pai, com os caminhos resolvidos a partir do diretorio do
         * ProcessFlow) e antes do exec, para que o programa da tarefa
         * enxergue o diretorio configurado. */
        if (diretorio_trabalho[0] != '\0') {
            if (chdir(diretorio_trabalho) != 0) {
                fprintf(stderr, "erro: nao foi possivel entrar em '%s' (tarefa '%s'): %s\n",
                        diretorio_trabalho, t->nome, strerror(errno));
                _exit(126);
            }
        }

        /* execvp substitui a imagem do processo: daqui em diante o filho E
         * o outro programa. Se der certo, NADA abaixo desta linha executa. */
        execvp(t->argv[0], t->argv);

        /* Se a execucao chegou aqui, o exec FALHOU. */
        fprintf(stderr, "erro: nao foi possivel executar '%s' (tarefa '%s'): %s\n",
                t->argv[0], t->nome, strerror(errno));

        /* _exit e obrigatorio: sem ele o filho voltaria para o loop de
         * comandos do main e passariam a existir DOIS ProcessFlow lendo do
         * mesmo teclado (comprovado em teste dirigido: ver relatorio). */
        _exit(127);
    }

    /* Caso 3: pid > 0 -> este codigo esta rodando NO PAI. */
    return pid;
}

/* ------------------------------------------------------------------ */
/* Coleta de processo                                                  */
/* ------------------------------------------------------------------ */

int task_collect(pid_t pid, const char *nome) {
    int status;

    /* waitpid com o PID exato. Usar waitpid(-1, ...) aqui pegaria
     * "qualquer filho", e o wait de um comando poderia coletar por engano
     * um job em background criado por outro. */
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "erro: waitpid do pid %d falhou: %s\n",
                (int)pid, strerror(errno));
        return -1;
    }

    return reportar_status(nome, pid, status);
}

/* ------------------------------------------------------------------ */
/* Modos de execucao                                                   */
/* ------------------------------------------------------------------ */

int exec_single(const Task *t) {
    int fd_in, fd_out;

    if (abrir_redirecionamentos(t, &fd_in, &fd_out) != 0) {
        return -1;  /* arquivo invalido: erro ja impresso, nada foi criado */
    }

    pid_t pid = task_spawn(t, fd_in, fd_out, NULL, 0);

    /* O pai nao usa esses descritores: o filho ja tem a copia dele. */
    fechar_se_aberto(fd_in);
    fechar_se_aberto(fd_out);

    if (pid < 0) {
        return -1;
    }
    return task_collect(pid, t->nome);
}

int exec_sequential(Task *ts[], int n) {
    int ultimo = 0;

    /* Sequencial: exec_single ja faz "criar + esperar" junto, entao chamar
     * ele em laco garante que a tarefa i+1 so nasce depois que a tarefa i
     * terminou. */
    for (int i = 0; i < n; i++) {
        ultimo = exec_single(ts[i]);
    }

    return ultimo;
}

int exec_parallel(Task *ts[], int n) {
    pid_t pids[MAX_TAREFAS];

    /* PRIMEIRO laco: cria TODOS os filhos, sem esperar por nenhum.
     * O enunciado exige que todas as tarefas sejam iniciadas antes que o
     * ProcessFlow espere pelo termino do grupo. Juntar os dois lacos em um
     * so transformaria este metodo em sequencial. */
    for (int i = 0; i < n; i++) {
        int fd_in, fd_out;

        if (abrir_redirecionamentos(ts[i], &fd_in, &fd_out) != 0) {
            pids[i] = -1;   /* erro ja impresso; esta tarefa nao roda */
            continue;
        }

        pids[i] = task_spawn(ts[i], fd_in, fd_out, NULL, 0);

        fechar_se_aberto(fd_in);
        fechar_se_aberto(fd_out);
    }

    /* SEGUNDO laco: agora sim o pai recolhe cada filho.
     *
     * A coleta acontece na ordem de criacao, e nao na ordem de termino, e
     * isso esta correto: se o segundo filho terminar antes do primeiro, o
     * waitpid do primeiro apenas espera mais um pouco, e o do segundo
     * retorna imediatamente. Nenhum filho deixa de ser coletado. */
    for (int i = 0; i < n; i++) {
        if (pids[i] > 0) {
            task_collect(pids[i], ts[i]->nome);
        }
    }

    return 0;
}

int exec_pipe(Task *ts[], int n) {
    pid_t pids[MAX_TAREFAS];
    int   fd_entrada_arquivo = -1;   /* input da PRIMEIRA tarefa, se houver */
    int   fd_saida_arquivo   = -1;   /* output da ULTIMA tarefa, se houver  */

    if (n < 2) {
        fprintf(stderr, "erro: 'run pipe' precisa de pelo menos duas tarefas\n");
        return -1;
    }

    /* Numa cadeia de pipe, o stdin de cada tarefa vem da tarefa anterior e
     * o stdout vai para a proxima. Sobram duas pontas livres: a entrada da
     * primeira e a saida da ultima. Sao essas — e so essas — que podem vir
     * de arquivo. */
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

    /* fd_leitura_anterior guarda a ponta de LEITURA do pipe criado na volta
     * anterior do laco: e por ela que a tarefa atual recebe a saida da
     * tarefa anterior. Comeca em -1 porque a primeira nao tem antecessora. */
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

        /* O filho precisa fechar a ponta de LEITURA do pipe que acabou de
         * herdar: ela pertence ao proximo elo da cadeia, nao a ele. */
        int fechar[1];
        int n_fechar = 0;
        if (i < n - 1) {
            fechar[n_fechar++] = fd[0];
        }

        pids[i] = task_spawn(ts[i], entrada, saida, fechar, n_fechar);
        criados++;

        /* ---- Fechamento no PAI: a parte que decide se trava ou nao ----
         *
         * O pai criou os pipes, mas nao le nem escreve neles. Toda ponta
         * que ele deixar aberta continua contando como "escritor vivo" para
         * o kernel, e o processo do outro lado espera para sempre por um
         * EOF que nao chega. Pior: o descritor esquecido e HERDADO pelos
         * filhos criados depois, e o problema se multiplica pela cadeia
         * (comprovado em teste dirigido: ver relatorio). */
        fechar_se_aberto(fd_leitura_anterior);

        if (i < n - 1) {
            close(fd[1]);                    /* quem escreve e o filho, nao o pai */
            fd_leitura_anterior = fd[0];     /* guarda a leitura para o proximo */
        } else {
            fd_leitura_anterior = -1;
        }
    }

    fechar_se_aberto(fd_leitura_anterior);
    fechar_se_aberto(fd_entrada_arquivo);
    fechar_se_aberto(fd_saida_arquivo);

    /* So agora o pai espera. Todos os processos da cadeia foram criados e
     * rodam ao mesmo tempo — e assim que um pipe funciona: os dados fluem
     * enquanto os processos executam, sem arquivo temporario. */
    for (int i = 0; i < criados; i++) {
        if (pids[i] > 0) {
            task_collect(pids[i], ts[i]->nome);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Jobs em background                                                  */
/* ------------------------------------------------------------------ */

/* Procura um job pelo id. Retorna NULL se nao existir. */
static Job *job_find(int id) {
    for (int i = 0; i < total_jobs; i++) {
        if (jobs[i].id == id) {
            return &jobs[i];
        }
    }
    return NULL;
}

/* Verifica, SEM BLOQUEAR, quais jobs ja terminaram.
 *
 * A flag WNOHANG e o que diferencia esta funcao do wait: se o filho ainda
 * esta rodando, o waitpid retorna 0 imediatamente em vez de travar. E por
 * isso que o comando jobs consegue mostrar o estado atual e devolver o
 * prompt na hora. */
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
        /* r == 0 significa "ainda rodando": nao ha nada a fazer. */
    }
}

int job_start(const Task *t) {
    int fd_in, fd_out;

    if (total_jobs >= MAX_JOBS) {
        fprintf(stderr, "erro: limite de %d jobs atingido\n", MAX_JOBS);
        return -1;
    }

    if (abrir_redirecionamentos(t, &fd_in, &fd_out) != 0) {
        return -1;
    }

    pid_t pid = task_spawn(t, fd_in, fd_out, NULL, 0);

    fechar_se_aberto(fd_in);
    fechar_se_aberto(fd_out);

    if (pid < 0) {
        return -1;
    }

    /* A diferenca entre start e run e exatamente esta: aqui NAO ha
     * task_collect. O pai registra o PID e volta imediatamente ao prompt;
     * a coleta fica para o comando wait (ou para o encerramento). */
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

    /* waitpid no PID DAQUELE job, nunca -1.
     *
     * Com -1 o ProcessFlow coletaria qualquer filho que terminasse primeiro:
     * o "wait 1" retornaria quando o job 2 acabasse, creditaria o resultado
     * ao job errado, e o job realmente esperado ficaria sem coleta — virando
     * um processo orfao ao encerrar. Comprovado em teste dirigido: ver
     * relatorio. */
    int status;
    if (waitpid(j->pid, &status, 0) < 0) {
        fprintf(stderr, "erro: waitpid do job %d (pid %d) falhou: %s\n",
                j->id, (int)j->pid, strerror(errno));
        j->ativo = 0;
        return -1;
    }

    j->ativo  = 0;
    j->codigo = codigo_de_status(status);

    /* Confirmacao apenas para quem esta digitando. Em execucao automatizada
     * o wait e silencioso, como no shell. */
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
