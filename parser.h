#ifndef PARSER_H
#define PARSER_H

#define MAX_TOKENS 64

int tokenize(char *linha, char *tokens[]);

void dispatch_command(int argc, char *argv[]);

int should_exit(void);

void set_modo_interativo(int interativo);
int  modo_interativo_ativo(void);

#endif
