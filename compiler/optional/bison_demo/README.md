# 实验四选做：flex + bison 等价实现

用 flex 做词法、bison 做语法分析的最小工程，目的是与手写 `compiler slr` 实现做对照。

## 文法

`parser.y` 中的文法与 `compiler/data/expr.grammar` 完全一致：

```
S : E
E : E ADD T | T
T : T MUL F | F
F : LPAR E RPAR | ID
```

终结符 `ADD MUL LPAR RPAR ID` 名称也对齐，方便和手写实现的输出逐项对比。

## 构建

需要 `flex`、`bison`、`gcc`：

```bash
make            # 生成 expr_parser 与 parser.output
make test       # 跑三条样例字符串
make clean
```

构建产物：
- `expr_parser` — 可执行文件，从 stdin 读一个表达式，accept 时输出 `ACCEPT`、reject 时输出 `REJECT`
- `parser.output` — bison 自动产生的完整 LALR(1) 状态机与 ACTION/GOTO 表
- `parser.tab.c`、`parser.tab.h`、`lex.yy.c` — bison/flex 生成的中间 C 文件

## 与手写实现的对照

`parser.output` 中的状态数、移进/归约/接受动作、GOTO 列对应到我们的 `compiler slr` 输出。两份表的状态编号顺序可能不同，但对每个状态闭包内的项目集做配对后，ACTION 单元格的动作应当一致。

bison 默认走 LALR(1)，对这份分层表达式文法而言，LALR(1) 与 SLR(1) 一致，所以这条等价性不仅成立，还能进一步验证手写 SLR 表的正确性。

## 环境

Linux 与 macOS 一般直接 `apt install flex bison` 或 `brew install flex bison` 即可。Windows 下推荐三选一：

```powershell
# 1. winget（用户级安装，无需管理员）
winget install --id WinFlexBison.win_flex_bison --scope user
# 装好后命令名是 win_flex / win_bison，本工程 Makefile 已经自动探测，无需额外配置。
# 安装后需打开一个新的终端窗口，让 PATH 生效。
```

```bash
# 2. MSYS2 (ucrt64)
pacman -S flex bison gcc make
```

```bash
# 3. WSL/Ubuntu
sudo apt install flex bison gcc make
```

Makefile 中 `FLEX` 与 `BISON` 变量会先查 `flex`/`bison`，找不到再回退到 `win_flex`/`win_bison`，所以三种安装方式都能直接 `make`。

### 服务器（无 sudo 权限）

在云服务器上若没有 root 权限，可以直接下载 `.deb` 包解压到用户目录：

```bash
mkdir -p ~/local && cd /tmp
apt-get download flex bison m4 libfl2 libfl-dev
for d in *.deb; do dpkg-deb -x "$d" ~/local; done

# 加到环境（可写入 ~/.bashrc）
export PATH=~/local/usr/bin:$PATH
export LD_LIBRARY_PATH=~/local/usr/lib/$(dpkg --print-architecture | sed 's/amd64/x86_64/')-linux-gnu:$LD_LIBRARY_PATH
export BISON_PKGDATADIR=~/local/usr/share/bison
export M4=~/local/usr/bin/m4
```

`BISON_PKGDATADIR` 让 bison 找到 `m4sugar.m4` 等运行时数据，`M4` 显式指向解压出来的 m4，避免 bison 在子进程里走系统 PATH 找不到。两个变量都设置好以后 `make` 即可正常构建。
