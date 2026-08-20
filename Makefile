# ============================================================
#  ProcessFlow - Orquestrador de processos
#  Infraestrutura de Software - CESAR School
#
#  Alvos:
#    make           compila o programa
#    make test      compila e roda a bateria de testes
#    make clean     apaga binario, objetos e saidas de teste
#    make entrega   limpa, valida e gera o ../maf.tar
#    make ajuda     mostra esta lista
# ============================================================

# O make executa as receitas com /bin/sh, que no Ubuntu e o dash e nao
# possui o comando "time". Como o alvo test mede o tempo das execucoes,
# forcamos o bash.
SHELL := /bin/bash

CC     = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -g

BIN    = processflow
OBJDIR = build
TAR    = maf.tar

SRC = $(wildcard *.c)
HDR = $(wildcard *.h)
OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(SRC))

.PHONY: all ajuda clean test entrega

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Os objetos ficam em build/ para nao poluir a raiz do projeto.
# Cada .o depende de TODOS os headers: assim, alterar um .h forca a
# recompilacao de tudo que possa depender dele.
# O "| $(OBJDIR)" e um pre-requisito de ordem: garante que o diretorio
# exista antes de compilar, sem forcar recompilacao quando ele muda.
$(OBJDIR)/%.o: %.c $(HDR) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(BIN)
	rm -f *.o
	rm -f resultado.txt historico.txt saida.txt

# ------------------------------------------------------------
#  Bateria de testes
#
#  Nota: cada linha de uma receita do make roda em um shell proprio.
#  Por isso o comando e o "echo $$?" que le o codigo de saida dele
#  precisam estar na MESMA linha, separados por ";" — em linhas
#  separadas o echo leria o $? de um shell recem-criado, sempre 0.
#  O ";" tambem faz o shell terminar com o status do echo (zero), o
#  que evita o make abortar nos testes cujo erro e o esperado.
# ------------------------------------------------------------
test: all
	@echo ""
	@echo "=========================================================="
	@echo " 1. MODO WORKFLOW - le teste1.pf, sem prompt, ecoando as linhas"
	@echo "=========================================================="
	@echo "\$$ ./$(BIN) teste1.pf"
	@./$(BIN) teste1.pf
	@echo ""
	@echo "=========================================================="
	@echo " 2. EXECUCAO SEQUENCIAL - sleep 3 + sleep 1, esperado ~4s"
	@echo "=========================================================="
	@echo "\$$ time ./$(BIN) teste_seq.pf"
	@time ./$(BIN) teste_seq.pf
	@echo ""
	@echo "=========================================================="
	@echo " 3. EXECUCAO PARALELA - as mesmas tarefas, esperado ~3s"
	@echo "=========================================================="
	@echo "\$$ time ./$(BIN) teste_par.pf"
	@time ./$(BIN) teste_par.pf
	@echo ""
	@echo "=========================================================="
	@echo " 4. ERRO FATAL - numero incorreto de argumentos"
	@echo "=========================================================="
	@echo "\$$ ./$(BIN) a b c"
	@./$(BIN) a b c; echo "codigo de saida: $$?   (esperado: diferente de 0)"
	@echo ""
	@echo "=========================================================="
	@echo " 5. ERRO FATAL - arquivo de workflow inexistente"
	@echo "=========================================================="
	@echo "\$$ ./$(BIN) nao_existe.pf"
	@./$(BIN) nao_existe.pf; echo "codigo de saida: $$?   (esperado: diferente de 0)"
	@echo ""
	@echo "=========================================================="
	@echo " 6. PROCESSOS ZUMBIS - esperado: 0"
	@echo "=========================================================="
	@echo "processos <defunct> encontrados: $$(ps -ef | grep -c '[d]efunct')"
	@echo ""
	@echo "Bateria concluida."

# ------------------------------------------------------------
#  Empacotamento para entrega
#  Limpa os gerados, confere que nao ha chamada proibida e gera o
#  ../maf.tar contendo apenas o que o enunciado pede.
# ------------------------------------------------------------
entrega: clean
	@echo "==> Verificando ausencia de system() e popen()..."
	@if grep -nE "(system|popen)[[:space:]]*\(" *.c; then \
		echo "ERRO: chamada proibida encontrada nos arquivos acima."; \
		exit 1; \
	fi
	@echo "    OK - nenhuma chamada proibida."
	@echo "==> Gerando ../$(TAR)..."
	@cd .. && tar -cf $(TAR) \
		--exclude='maf/.git' \
		--exclude='maf/build' \
		--exclude='maf/PLANEJAMENTO.md' \
		--exclude='maf/ROTEIRO_DE_TESTES.md' \
		--exclude='maf/diario.md' \
		--exclude='maf/MATERIAL_RELATORIO.md' \
		maf/
	@echo "==> Conteudo do pacote:"
	@cd .. && tar -tvf $(TAR)
	@echo ""
	@echo "Pronto: ../$(TAR)"
	@echo "Confira acima que evidencias.log esta na lista e que os"
	@echo "arquivos de trabalho pessoal NAO estao."

ajuda:
	@echo "make          compila o programa (./$(BIN))"
	@echo "make test     compila e roda a bateria de testes"
	@echo "make clean    apaga binario, objetos e saidas de teste"
	@echo "make entrega  limpa, valida e gera o ../$(TAR)"
