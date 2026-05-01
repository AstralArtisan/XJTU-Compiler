# XJTU Compiler

西安交通大学编译器设计专题实验 — 逐步构建完整编译器。

## 项目结构

```
compiler/          C 编译器源码
  include/         头文件
  src/             源码
  data/            DFA 定义文件
  tests/           测试用例
web/               前端可视化
```

## 构建与运行

```bash
cd compiler
make clean && make

# Lab1: DFA 模拟
./compiler dfa data/simple.dfa --enumerate 3
./compiler dfa data/simple.dfa --test "ab"
./compiler dfa data/lexer.dfa --format=json

# Lab2: 词法分析（手写 scanner）
./compiler scan -f tests/scan/sample.c

# Lab2: 词法分析（表驱动 scanner，基于 DFA 引擎）
./compiler scan -f tests/scan/sample.c --table data/lexer.dfa

# JSON 输出（供前端可视化）
./compiler scan -f tests/scan/sample.c --format=json
```

## 实验进度

| 实验 | 内容 | 状态 |
|------|------|------|
| Lab1 | DFA 引擎 | ✅ |
| Lab2 | 词法分析器 | ✅ |
| Lab3 | LR(0) 项目集 | 🔲 |
| Lab4 | SLR(1) 分析表 | 🔲 |
| Lab5 | 语义分析 | 🔲 |
| Lab6 | 中间代码生成 | 🔲 |
