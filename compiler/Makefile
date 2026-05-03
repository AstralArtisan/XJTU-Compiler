CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude

TARGET := compiler
SRC := src/token.c src/scanner.c src/dfa.c src/table_scanner.c src/main.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean test deploy-web check-web

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

test: $(TARGET)
	@echo "=== scan mode 1 ===" && printf "1\n5\nid if 485 841.6541 www\n" | ./$(TARGET) scan && echo
	@echo "=== scan unsigned numbers ===" && printf "1\n5\nid if 485 841.6541 www\n" | ./$(TARGET) scan && echo
	@echo "=== scan mode 2 ===" && printf "2\nwhile(true){int a=-123;}\n" | ./$(TARGET) scan && echo
	@echo "=== scan file ===" && ./$(TARGET) scan -f tests/scan/test1.c && echo
	@echo "=== scan regression file ===" && ./$(TARGET) scan -f tests/scan/regression.c && echo
	@echo "=== scan invalid operators ===" && printf "2\nint x & y | z && q || r;\n" | ./$(TARGET) scan && echo
	@echo "=== scan compare ===" && ./$(TARGET) scan --compare -f tests/scan/sample.c && echo
	@echo "=== dfa enumerate ===" && ./$(TARGET) dfa data/simple.dfa --enumerate 3 && echo
	@echo "=== dfa trace accept ===" && ./$(TARGET) dfa data/simple.dfa --test aa --trace && echo
	@echo "=== dfa trace reject ===" && ./$(TARGET) dfa data/simple.dfa --test aba --trace || true && echo
	@echo "=== dfa json ===" && ./$(TARGET) dfa data/simple.dfa --format=json && echo
	@echo "=== scan json ===" && ./$(TARGET) scan -f tests/scan/sample.c --format=json | head -5

deploy-web:
	cp ../web/index.html ../web/style.css ../web/app.js ../docs/
	@echo "docs/ synced from web/"

check-web:
	cmp -s ../web/index.html ../docs/index.html
	cmp -s ../web/style.css ../docs/style.css
	cmp -s ../web/app.js ../docs/app.js
	@echo "docs/ matches web/"
