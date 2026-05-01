CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Iinclude

TARGET := scanner_table
BRIDGE := lab1_bridge
SRC := src/table_scanner.c src/main.c
OBJ := $(SRC:.c=.o)

.PHONY: all clean

all: $(TARGET) $(BRIDGE)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

$(BRIDGE): src/lab1_bridge.c
	$(CC) $(CFLAGS) -o $@ src/lab1_bridge.c

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET) $(BRIDGE)
