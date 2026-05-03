# XJTU Compiler

西安交通大学编译器设计专题实验 — 逐步构建完整编译器。

**在线演示**: [https://astralartisan.github.io/XJTU-Compiler/](https://astralartisan.github.io/XJTU-Compiler/)

## 项目结构

```
compiler/           C 编译器源码
  include/          头文件 (dfa.h, token.h, scanner.h, table_scanner.h)
  src/              源码 (dfa.c, scanner.c, table_scanner.c, main.c, token.c)
  data/             DFA 定义文件 (.dfa 格式)
  tests/            测试用例
docs/               前端可视化 (GitHub Pages 部署目录，上传 GitHub)
web/                前端可视化本地开发目录 (不上传 GitHub)
```

## 构建

```bash
cd compiler
make clean && make
```

需要 GCC + C11。在 Linux (aarch64) 和 Windows (MinGW) 上均可编译。

## 使用

### DFA 模拟 (Lab1)

```bash
# 交互模式：打印 DFA 信息，枚举可接受串，测试字符串
./compiler dfa data/simple.dfa

# 枚举长度 ≤ N 的所有可接受串
./compiler dfa data/simple.dfa --enumerate 3

# 测试单个字符串
./compiler dfa data/simple.dfa --test "aa"

# 输出识别过程
./compiler dfa data/simple.dfa --test "aba" --trace

# JSON 输出（供前端可视化）
./compiler dfa data/simple.dfa --format=json
```

### 词法分析 (Lab2)

```bash
# 表驱动 scanner（默认，基于 Lab1 DFA 引擎）
./compiler scan -f tests/scan/sample.c

# 显式指定 DFA 规则文件
./compiler scan -f tests/scan/sample.c --table data/lexer.dfa

# 手写 scanner（选做对照）
./compiler scan -f tests/scan/sample.c --impl=hand

# 对比手写和表驱动输出
./compiler scan --compare -f tests/scan/sample.c

# JSON 输出
./compiler scan -f tests/scan/sample.c --format=json

# 交互模式（mode 1: 逐词分类，mode 2: 整行分析）
./compiler scan
```

### 输出到文件

```bash
./compiler scan -f input.c -o tokens.out
```

## 在线可视化

访问 [https://astralartisan.github.io/XJTU-Compiler/](https://astralartisan.github.io/XJTU-Compiler/)，包含两个视图：

### DFA Explorer

- 通过表单或 JSON 定义 DFA（字母表、状态数、起始状态、接受状态、转移表）
- 可视化状态转移图（Canvas 绘制，节点颜色区分起始/接受状态）
- 输入字符串测试是否被接受，支持逐步动画演示状态转移过程
- 枚举指定长度内的所有可接受串
- 可上传 `./compiler dfa --format=json` 生成的 JSON 文件

### Lexical Analyzer

- 在编辑器中输入源代码，点击 Scan 即可在前端完成词法分析
- Token 流以表格展示（类型、词素、行:列），不同类型用颜色区分
- 源码高亮显示各 token 类型
- 可上传 `./compiler scan --format=json` 生成的 JSON 文件查看后端分析结果

## DFA 文件格式

支持两种格式，自动检测：

**旧格式**（lab1 兼容）：
```
ab          # 字母表
4           # 状态数
1           # 起始状态
4           # 接受状态
2 3         # 状态 1 的转移
4 3         # 状态 2 的转移
2 4         # 状态 3 的转移
4 4         # 状态 4 的转移
```

**扩展格式**（支持字符类、token 标注、关键字）：
```
CHARCLASS:
  LETTER  a-d f-z A-D F-Z _
  DIGIT   0-9
  ...
END

STATES: 102
START: 0

ACCEPT:
  1=ID 2=NUM 3=FLOAT ...

TRANS:
  0 LETTER -> 1
  0 DIGIT  -> 2
  ...
END

KEYWORDS:
  int=INT float=FLOAT void=VOID ...
END
```

## 架构

```
                    ┌─────────────┐
                    │   main.c    │  子命令分发
                    └──────┬──────┘
                 ┌─────────┼─────────┐
                 ▼         ▼         ▼
            ┌────────┐ ┌────────┐ ┌────────┐
            │ dfa.c  │ │scanner │ │ table  │
            │ (Lab1) │ │  .c    │ │scanner │
            │        │ │ (Lab2) │ │  .c    │
            └────────┘ └────────┘ └───┬────┘
                 ▲                    │
                 └────── dfa_step() ──┘
                   Lab1 DFA 引擎驱动 Lab2 表驱动 scanner
```

## 实验进度

| 实验 | 内容 | 状态 |
|------|------|------|
| Lab1 | DFA 引擎 | ✅ |
| Lab2 | 词法分析器（手写 + 表驱动） | ✅ |
| Lab3 | LR(0) 项目集 | 🔲 |
| Lab4 | SLR(1) 分析表 | 🔲 |
| Lab5 | 语义分析 | 🔲 |
| Lab6 | 中间代码生成 | 🔲 |
