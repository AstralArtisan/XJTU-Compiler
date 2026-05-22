# XJTU Compiler

西安交通大学编译器设计专题实验 — 逐步构建完整编译器（词法 → 语法 → 语义 → 中间代码 → 运行时 → 可执行代码）。

**在线演示**: <https://astralartisan.github.io/XJTU-Compiler/>

## 项目结构

```
compiler/                  C 编译器源码
  include/                 头文件
    dfa.h token.h scanner.h table_scanner.h     # Lab1 / Lab2
    grammar.h lr0.h slr.h                       # Lab3 / Lab4
    ast.h symtab.h parser.h semantic.h          # Lab5
  src/                     对应 .c 文件
  data/                    DFA / 文法定义文件
  tests/                   词法 / 文法回归用例
  optional/                自动化工具对照（flex + bison demo 等）
docs/                      前端 GitHub Pages 部署目录
web/                       前端开发目录 (.gitignore，不上传 GitHub)
tests/                     33 份 .src 端到端测试 + run_parse.py 回归脚本
```

## 构建

```bash
cd compiler
make clean && make
```

需要 GCC + C11。在 Linux (aarch64) 与 Windows (MinGW) 均可编译，零警告（`-Wall -Wextra -Wpedantic`）。

## 子命令一览

```bash
./compiler dfa   <file.dfa>     [--enumerate N] [--test STR] [--trace] [--format=json]
./compiler scan  [-f IN [-o OUT]] [--impl=table|--impl=hand] [--table DFA] [--compare] [--format=json]
./compiler lr0   <file.grammar> [--show=closure|goto|conflicts|productions|all] [--format=json]
./compiler slr   <file.grammar> [--show=first|follow|action|goto|conflicts|all] [--format=json]
./compiler parse -f IN [-o OUT] [--grammar PATH] [--dfa PATH] [--show=ast|symtab|errors|all] [--format=json] [--trace=json]
```

### DFA 模拟 (Lab1)

```bash
./compiler dfa data/simple.dfa                # 交互：信息 + 枚举 + 测试
./compiler dfa data/simple.dfa --enumerate 3  # 长度 ≤ 3 的全部可接受串
./compiler dfa data/simple.dfa --test "aa"    # 单串测试
./compiler dfa data/simple.dfa --test "aba" --trace
./compiler dfa data/simple.dfa --format=json  # JSON（前端消费）
```

### 词法分析 (Lab2)

```bash
./compiler scan -f tests/scan/sample.c                              # 表驱动（默认，基于 Lab1 DFA）
./compiler scan -f tests/scan/sample.c --impl=hand                  # 手写 scanner（选做对照）
./compiler scan --compare -f tests/scan/sample.c                    # 两种实现一致性比对
./compiler scan -f tests/scan/sample.c --format=json
./compiler scan                                                      # 交互式（mode 1/2）
```

### LR(0) 项目集规范族 (Lab3)

```bash
./compiler lr0 data/expr.grammar
./compiler lr0 data/expr_ambig.grammar --show=conflicts             # 看 shift-reduce 冲突
./compiler lr0 data/expr.grammar --format=json
```

### SLR(1) 分析表 (Lab4)

```bash
./compiler slr data/expr.grammar                                    # FIRST/FOLLOW + ACTION/GOTO
./compiler slr data/expr.grammar --show=first
./compiler slr data/slr_demo.grammar                                # LR(0) 撞 reduce-reduce、SLR 消解
./compiler slr data/dangling_if.grammar --show=conflicts            # SLR 仍剩 dangling-else
./compiler slr data/expr.grammar --format=json
```

冲突处理：shift 与 reduce 撞同一格时按 yacc/bison 默认采用 prefer-shift，冲突仍被记录到 `conflicts[]` 用于调试。

### 完整解析 + 语义分析 (Lab5)

支持两种入参，等价产出 AST / 符号表 / 错误列表：

```bash
# 模式 A：内部 lexer 直连（短路写法）
./compiler parse -f tests/1.src
./compiler parse -f tests/1.src --show=ast
./compiler parse -f tests/6.src --show=errors                       # 仅看错误

# 模式 B：PPT 要求的三件入参——把 Lab2 输出的 token 流喂进 Lab5
./compiler scan  -f tests/1.src --format=json > tokens.json
./compiler parse --tokens tokens.json --grammar data/c_lite.grammar --show=all

# 通用选项
./compiler parse -f tests/1.src --format=json                       # 给前端用
./compiler parse -f tests/1.src --trace=json                        # 附加 steps[]/productions[] 供剧场重放
python tests/run_parse.py                                            # 端到端能力演示：跑全部 33 个 .src
```

默认文法 `data/c_lite.grammar`，默认词法 DFA `data/lexer.dfa`。`-f` 与 `--tokens` 互斥；两者都缺会打印 usage。

## 在线可视化

[https://astralartisan.github.io/XJTU-Compiler/](https://astralartisan.github.io/XJTU-Compiler/) 提供五个并列视图：

### DFA Explorer (Lab1)

表单或 JSON 定义 DFA，Canvas 状态图，字符串测试 + 逐步动画，枚举可接受串。

### Lexical Analyzer (Lab2)

源代码编辑器 + 一键 Scan，Token 流以彩色表格呈现；浏览器内置 tokenizer 兼任离线 fallback。

### LR(0) Builder (Lab3)

输入 CFG，Canvas 状态图显示项目集与 Goto 边，冲突状态高亮，右侧面板列闭包与出边。

### SLR(1) Builder (Lab4)

FIRST/FOLLOW 卡片 + ACTION/GOTO 二维表（shift 蓝、reduce 绿、accept 金、冲突红虚线高亮），冲突面板列出被保留与丢弃的两条动作来源。

### Parse & Semantic (Lab5)

- **结果模式**：AST 折叠树 + 嵌套作用域符号表盒子 + 错误列表（LEX/SYN/SEM 三色区分）
- **剧场模式**：勾选后跑 `--trace=json`，四列联动单步动画——Token 流（高亮 lookahead）/ 状态栈 / AST 增量画布（节点按 reduce 顺序浮现）/ 符号表
- **批量看板**：Run all 33 一键跑全部测试用例，色块矩阵给出 pass / fail-as-expected / mismatch 三类分布
- **用例下拉**：33 份 `.src` 已嵌入前端，离线也能加载

## 架构

```
       ┌─────────┐
       │ main.c  │   子命令分发
       └────┬────┘
            │
   ┌────────┼────────┬────────┬──────────┐
   ▼        ▼        ▼        ▼          ▼
┌─────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────────┐
│ dfa │ │scan- │ │ lr0  │ │ slr  │ │  parse   │
│Lab1 │ │ ner  │ │ Lab3 │ │ Lab4 │ │  Lab5    │
└──┬──┘ │Lab2  │ └──┬───┘ └──┬───┘ └────┬─────┘
   │    └──┬───┘    │        │          │
   └───────┴────────┘        │          │
   dfa_step() drives table_scanner      │
                              │          │
                grammar.c ────┼──────────┤
                              │          │
                lr0.c ────────┴──────────┤
                                         │
                slr.c ───────────────────┤   (FIRST/FOLLOW + ACTION/GOTO)
                                         │
                table_scanner ───────────┤   (token 流)
                                         │
                parser.c ─ semantic.c ─┐ │   (LR 驱动 + 语义遍历)
                ast.c ─ symtab.c ──────┘
```

每个新增模块都遵守 `include/xxx.h` + `src/xxx.c` 的拆分约定，可单独编译验证。前端按实验切分 `docs/js/{shared,dfa,scanner,lr0,slr,parse}.js`，新实验加一个文件即可。

## DFA 文件格式

支持两种，自动检测：

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

**扩展格式**（字符类 / token 标注 / 关键字）：

```
CHARCLASS:
  LETTER  a-d f-z A-D F-Z _
  DIGIT   0-9
END

STATES: 102
START: 0

ACCEPT:
  1=ID 2=NUM 3=FLOAT ...

TRANS:
  0 LETTER -> 1
  0 DIGIT  -> 2
END

KEYWORDS:
  int=INT float=FLOAT void=VOID ...
END
```

## 文法文件格式

```
%start P
%terminals INT FLOAT_KW VOID IF ELSE WHILE RETURN PRINT INPUT
%terminals ID NUM FLOAT_LIT ADD SUB MUL DIV LT LE EQ GT GE NE
%terminals ASG LPAR RPAR LBK RBK LBR RBR CMA SCO

P -> Decls
Decls -> Decls Decl | Decl
...
```

注释用 `#`；`->` / `::=` / `→` 三种箭头都支持；`|` 分隔同一行的多条候选；空产生式写 `epsilon` / `EPSILON` / `ε`。终结符名建议与 lab2 token kind 对齐（如 ADD、ID、SCO），让 SLR 表能直接消费词法器的输出。

## 实验进度

| 实验 | 内容 | 状态 |
|------|------|------|
| Lab1 | DFA 引擎 | ✅ |
| Lab2 | 词法分析器（手写 + 表驱动） | ✅ |
| Lab3 | LR(0) 项目集规范族 | ✅ |
| Lab4 | SLR(1) 分析表（含 flex+bison 等价对照） | ✅ |
| Lab5 | SLR 驱动语义分析（AST + 符号表 + 类型检查 + 剧场可视化 + 33 用例回归） | ✅ |
| Lab6 | 中间代码生成 | 🔲 |
| Lab7 | 内存映射 | 🔲 |
| Lab8 | 目标代码生成 | 🔲 |

## 服务器 API

后端服务托管在 `https://lines-eternal-ray-fighting.trycloudflare.com`，前端自动探活。本地启动可用 `python compiler/server.py --port 8080`。

| 路径 | 方法 | 输入 | 用途 |
|------|------|------|------|
| `/api/health` | GET | — | 服务存活探测 |
| `/api/scan`   | POST | `{"source":"..."}` | 词法分析 |
| `/api/dfa`    | POST | `{"action":"test/enumerate/json", "dfa_file":"..."}` | DFA 操作 |
| `/api/lr0`    | POST | `{"grammar":"..."}` 或 `{"grammar_file":"..."}` | LR(0) 项目集 |
| `/api/slr`    | POST | 同上 | SLR(1) 分析表 |
| `/api/parse`  | POST | `{"source":"...","trace":bool}` | 完整解析；trace=true 附带 steps[] |
