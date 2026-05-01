# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 仓库定位

大学课程"编译器设计专题实验"的工作目录。8 次实验逐步构建完整编译器（词法→语法→语义→中间代码→运行时→可执行代码）。验收从第 7 次开始。成绩 = 平时×0.3 + 报告×0.7。

## 老师的核心意图

实验之间有代码级关联，不是独立作业：
- **Lab1** DFA 引擎是基础设施，后续实验复用
- **Lab2** scanner 应基于 lab1 的 DFA 引擎（表驱动方式）
- **Lab3** LR(0) 项目集 → **Lab4** SLR(1) 分析表 → **Lab5+** 语义分析...
- 每个实验的选做空间大，老师鼓励 AI 赋能、Git 管理、前端可视化

关键引用（lab3 PPT slide 8）：
> "dfa_in.dfa 该文件是实验一的输入，根据 dfa 表输出可接受字符流，该文件在实验二中加载作为词法分析的规则文件"

## 项目结构

```
编译器实验/                    # 本地工作目录
├── compiler/                 # 统一编译器项目（上传 GitHub + 部署服务器）
│   ├── Makefile
│   ├── include/              # dfa.h, token.h, scanner.h, table_scanner.h, ...
│   ├── src/                  # dfa.c, token.c, scanner.c, table_scanner.c, main.c, ...
│   ├── data/                 # DFA 定义文件（.dfa 格式）
│   └── tests/                # 按阶段组织的测试用例
├── web/                      # 前端可视化（GitHub Pages）
├── lab1/ ~ lab8/             # 各实验的 PPT、报告、开发代码（不上传 GitHub）
├── CLAUDE.md
└── 报告规范.md
```

## GitHub 仓库

地址：`git@github.com:AstralArtisan/XJTU-Compiler.git`

仓库只包含 compiler/、web/、README.md、.gitignore。lab 文件夹不上传（含老师 PPT 和个人信息）。

## 每次实验的工作流

1. 用户把 PPT/PDF 放到 `labX/`
2. 读 PPT，在 `labX/` 里写该实验的独立代码做开发和测试
3. 代码稳定后整合到 `compiler/` 作为新模块
4. 前端 `web/` 加对应的可视化 tab
5. git commit + push 到 GitHub
6. scp `compiler/` 到服务器做最终验证
7. 写实验报告到 `labX/实验报告.md`（遵守 `报告规范.md`）

## compiler/ 架构

子命令式 CLI：

```bash
./compiler dfa <file.dfa> [--enumerate N] [--test STR] [--format=json]
./compiler scan [-f IN [-o OUT]] [--table DFA] [--format=json]
./compiler parse [-f IN] [--format=json]          # lab3+ 加入
```

核心模块：
- `dfa.c` — Lab1 DFA 引擎（加载/验证/模拟/枚举/单步转移/JSON输出）
- `scanner.c` — Lab2 手写词法分析器
- `table_scanner.c` — Lab2 表驱动词法分析器（调用 dfa.c 的 dfa_step()）
- `token.c` — Token 类型定义和名称查找

统一 DFA 格式（.dfa 文件）：
- 兼容 lab1 旧格式（纯数字五元组）
- 扩展格式支持 CHARCLASS/TRANS/ACCEPT/KEYWORDS 段
- dfa.c 自动检测格式

## 选做策略（按优先级）

1. **前端可视化**（最高优先级）— DFA 状态图 + Token 流展示 + 后续阶段逐步添加
2. **Git 仓库**— 清晰提交历史，老师多次强调
3. **多种实现方式**— 手写 scanner vs 表驱动 scanner，输出一致性对比
4. **DFA 格式多样化**— 支持 .dfa 旧格式和扩展格式

## 测试环境

本地 Windows 编辑，**最终在云服务器验证**：

```sh
ssh wuji@igw.netperf.cc -p 2123
```

Linux (aarch64) + GCC 11 + C11。上传后 `make clean && make`。

## 实验报告

`报告规范.md` 是硬约束，写报告时读它并调用 `/lab-report` skill。要点：
- 顶级标题 `# 实验X：实验名称`，紧跟 `[TOC]`
- 章节：实验内容（必做）/ 实验结果（必做）/ 实验内容（选做）/ 实验结果（选做）/ 个人总结
- 不贴源代码，写设计思路与实现过程
- 从学生视角写，不引用 PPT 页码
- 每张截图下面写一段分析
- 个人总结 150–400 字

## 报告中的必做/选做定位

- **Lab1 必做**：通用 DFA 引擎（五元组加载、验证、枚举、识别）
- **Lab2 必做**：表驱动 scanner（基于 lab1 DFA 引擎，体现实验关联）
- **Lab2 选做**：手写 scanner（另一种实现方式）、前端可视化、Git 管理
