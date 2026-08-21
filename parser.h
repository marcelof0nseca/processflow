#ifndef PARSER_H
#define PARSER_H

#define MAX_TOKENS 64  /* numero maximo de palavras em uma linha de comando */

/* Quebra "linha" em palavras separadas por espaco, tab ou quebra de linha.
 * Preenche tokens[] e coloca NULL na posicao seguinte a ultima palavra.
 * Retorna quantas palavras foram encontradas (0 se a linha estava vazia
 * ou so tinha espacos).
 *
 * ATENCAO: modifica a string "linha" (o strtok troca os separadores por '\0'). */
int tokenize(char *linha, char *tokens[]);

/* Recebe a linha ja quebrada em palavras e executa o comando correspondente.
 * Comando desconhecido ou mal formado: imprime erro e retorna normalmente,
 * porque o ProcessFlow nao pode morrer por causa de um comando invalido. */
void dispatch_command(int argc, char *argv[]);

/* Retorna 1 depois que o comando "exit" foi executado. O loop de leitura
 * usa isso para saber que deve parar. */
int should_exit(void);

/* Modo de operacao do ProcessFlow.
 *
 * 1 = sessao interativa de verdade, com alguem digitando no terminal. O
 *     prompt e as mensagens de confirmacao ("tarefa X cadastrada") fazem
 *     sentido e ajudam o usuario.
 * 0 = entrada vinda de um arquivo .pf ou de stdin redirecionado. A saida
 *     deve conter apenas o eco das linhas (no modo workflow) e o que os
 *     programas executados produzirem — nada gerado pelo ProcessFlow.
 *
 * E o mesmo criterio que o bash usa: com a entrada redirecionada ele nao
 * imprime prompt. Definido uma vez pelo main, antes do laco de leitura. */
void set_modo_interativo(int interativo);
int  modo_interativo_ativo(void);

#endif
