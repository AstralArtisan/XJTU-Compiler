CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude

TARGET := compiler
SRC := src/token.c src/scanner.c src/dfa.c src/table_scanner.c src/main.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

test: $(TARGET)
	@echo "=== scan mode 1 ===" && printf "1\n5\nid if 485 841.6541 www\n" | ./$(TARGET) scan && echo
	@echo "=== scan mode 2 ===" && printf "2\nwhile(true){int a=0;}\n" | ./$(TARGET) scan && echo
	@echo "=== scan file ===" && ./$(TARGET) scan -f tests/scan/test1.c && echo
	@echo "=== dfa enumerate ===" && ./$(TARGET) dfa data/simple.dfa --enumerate 3 && echo
	@echo "=== dfa test ===" && ./$(TARGET) dfa data/simple.dfa --test ab && echo
	@echo "=== dfa json ===" && ./$(TARGET) dfa data/simple.dfa --format=json && echo
	@echo "=== scan json ===" && ./$(TARGET) scan -f tests/scan/sample.c --format=json | head -5
