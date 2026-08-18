CC     = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -g
BIN    = processflow

SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)
HDR = $(wildcard *.h)

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

# Todo .o depende dos headers: assim, mexer em um .h forca recompilar tudo
# (sem isso, editar um .h nao dispara rebuild e da erro estranho depois).
%.o: %.c $(HDR)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

test: all
	@echo "=== teste1.pf (modo workflow) ==="
	./$(BIN) teste1.pf
	@echo ""
	@echo "=== erro fatal: numero incorreto de argumentos ==="
	-./$(BIN) a b c
	@echo ""
	@echo "=== erro fatal: arquivo de workflow inexistente ==="
	-./$(BIN) nao_existe.pf
