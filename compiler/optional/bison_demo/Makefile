# bison_demo Makefile
# 依赖：flex bison gcc
# 用法：
#   make           # 生成 expr_parser，并产出 parser.output 用于查看 ACTION/GOTO 表
#   make test      # 跑几条字符串验证
#   make clean

CC := gcc
CFLAGS := -O2 -Wall -Wno-unused-function

# 自动探测 flex/bison（Linux/macOS/MSYS2）或 win_flex/win_bison（winflexbison on Windows）
FLEX  ?= $(shell command -v flex 2>/dev/null || echo win_flex)
BISON ?= $(shell command -v bison 2>/dev/null || echo win_bison)

ifeq ($(OS),Windows_NT)
EXEEXT := .exe
else
EXEEXT :=
endif

TARGET := expr_parser$(EXEEXT)

.PHONY: all test clean

all: $(TARGET)

# bison -d 生成头文件，-v 输出 parser.output 含完整状态机和分析表
parser.tab.c parser.tab.h parser.output: parser.y
	$(BISON) -d -v parser.y

lex.yy.c: lexer.l parser.tab.h
	$(FLEX) lexer.l

$(TARGET): lex.yy.c parser.tab.c
	$(CC) $(CFLAGS) -o $@ lex.yy.c parser.tab.c

test: $(TARGET)
	@echo "=== accept: a + b * c ==="
	@echo 'a + b * c' | ./$(TARGET)
	@echo "=== accept: (a + b) * c ==="
	@echo '(a + b) * c' | ./$(TARGET)
	@echo "=== reject: a +"
	@echo 'a +' | ./$(TARGET); true

clean:
	rm -f parser.tab.c parser.tab.h parser.output lex.yy.c expr_parser expr_parser.exe
