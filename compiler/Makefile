CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude
PYTHON ?= python

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
else
EXEEXT :=
endif

TARGET := compiler$(EXEEXT)
SRC := src/token.c src/scanner.c src/dfa.c src/table_scanner.c \
       src/grammar.c src/lr0.c src/slr.c \
       src/ast.c src/symtab.c src/parser.c src/semantic.c \
       src/main.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean test deploy-web check-web

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) compiler compiler.exe

test: $(TARGET)
	$(PYTHON) tools/run_tests.py

deploy-web:
	$(PYTHON) tools/web_sync.py

check-web:
	$(PYTHON) tools/web_sync.py --check
