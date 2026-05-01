# Web 前端设计规范

本文档描述 XJTU Compiler Visualizer 的前端设计思路、技术选型和视觉规范，确保后续实验扩展时风格一致。

## 设计理念

参考 Linear、Vercel Dashboard、Raycast 等现代开发者工具的设计语言：

- **克制**：不堆砌装饰，用间距和层级说话
- **功能优先**：每个元素都有明确用途，没有纯装饰性组件
- **深色主题**：开发者工具的自然选择，减少视觉疲劳
- **渐进展示**：空状态有引导文字，操作后才出现结果

## 技术栈

| 技术 | 用途 | 选择理由 |
|------|------|---------|
| 原生 HTML/CSS/JS | 页面结构和交互 | 零依赖，GitHub Pages 直接部署 |
| Canvas 2D API | DFA 状态转移图 | 完全控制渲染细节，不依赖图形库 |
| Inter (Google Fonts) | UI 字体 | 现代无衬线，可读性好 |
| JetBrains Mono (Google Fonts) | 代码/等宽字体 | 专为代码设计，字符区分度高 |
| Python http.server | API 服务 | 包装 C 编译器，轻量无依赖 |

不使用任何前端框架（React/Vue/Angular），不使用 npm/webpack，保持极简。

## 色彩系统

基于 Tailwind CSS 的 Zinc 色板，搭配语义色：

```
背景层级:
  --bg:    #09090b    最深背景（页面底色）
  --bg-1:  #18181b    卡片/区块背景
  --bg-2:  #27272a    按钮/输入框背景、hover 状态
  --bg-3:  #3f3f46    活跃状态、分割线

文字层级:
  --text:    #fafafa    主文字（标题、正文）
  --text-1:  #a1a1aa    次要文字（标签、描述）
  --text-2:  #71717a    辅助文字（提示、占位符）
  --text-3:  #52525b    最弱文字（行号、分隔）

语义色:
  --blue:    #3b82f6    主操作色（按钮、链接、焦点）
  --green:   #22c55e    成功/接受状态
  --red:     #ef4444    错误/拒绝状态
  --purple:  #a78bfa    关键字高亮
  --orange:  #f59e0b    数字/字面量高亮
  --cyan:    #06b6d4    运算符高亮
```

每个语义色都有对应的半透明背景色（如 `--green-bg: rgba(34,197,94,.1)`），用于结果框、标签等。

## Token 高亮配色

词法分析器中不同 token 类型的颜色：

| Token 类型 | CSS 类 | 颜色 | 示例 |
|-----------|--------|------|------|
| 关键字 (int, if, while...) | `.tk-keyword` | 紫色 #a78bfa | `int`, `while` |
| 标识符 | `.tk-id` | 白色 #fafafa | `gcd`, `x` |
| 数字 | `.tk-num` | 橙色 #f59e0b | `123`, `3.14` |
| 浮点数 | `.tk-float` | 橙色 #f59e0b | `1.5e-3` |
| 运算符 | `.tk-op` | 青色 #06b6d4 | `+`, `!=`, `&&` |
| 分隔符 | `.tk-delim` | 灰色 #71717a | `(`, `;`, `{` |
| 错误 | `.tk-err` | 红色 #ef4444 | 波浪下划线 |

## 字体规范

```
UI 字体:   'Inter', system-ui, -apple-system, sans-serif
代码字体:  'JetBrains Mono', 'Fira Code', monospace
```

关键设置：
- `font-feature-settings: 'liga' 0, 'calt' 0` — 禁用连字，确保 `!=` 不会显示为 `≠`
- `font-variant-numeric: tabular-nums` — 表格中数字等宽对齐
- 基础字号 15px，行高 1.6

字号层级：
| 用途 | 大小 |
|------|------|
| 页面标题 h2 | 1.15rem |
| 区块标题 h3 | 1.05rem |
| 正文/按钮 | .9rem |
| 代码/表格 | .9rem (mono) |
| 标签 | .88rem |
| 提示/辅助 | .82rem |

## 间距系统

基于 4px 递增：

| 用途 | 值 |
|------|-----|
| 卡片内边距 | 20px |
| 卡片间距 | 16px |
| 字段间距 | 16px |
| 字段内部（label→input） | 4px |
| 按钮内边距 | 8px 16px |
| 按钮间距 | 8px |

## 圆角

```
--radius:    10px    卡片/区块
--radius-sm:  6px    按钮/输入框/标签
```

## 组件规范

### 按钮

两种样式：
- **Primary** (`.btn-primary`)：蓝色实心，用于主操作（Render、Scan、Test）
- **Secondary** (`.btn-secondary`)：深灰边框，用于次要操作（Load Sample、Upload）

状态：hover 变亮，active 缩放 0.97，focus-visible 蓝色外框。

### 卡片 (`.section`)

深灰背景 `--bg-1`，1px 边框 `--border`，10px 圆角。内部用 flex column + gap 16px 排列。

### 输入框

深黑背景 `--bg`，1px 边框，focus 时蓝色边框 + 蓝色光晕（`box-shadow: 0 0 0 3px var(--blue-glow)`）。

### 表格

sticky 表头，深灰背景。行 hover 时微弱蓝色高亮。等宽数字。

## 布局模式

### DFA Explorer
```
┌─────────────────────────────────────┐
│  State Transition Graph (hero)      │  ← 全宽，380px 高
└─────────────────────────────────────┘
┌──────────────────────┬──────────────┐
│  DFA Definition      │  Test String │  ← 2:1 网格
│  (Form / JSON)       │  Enumerate   │
│  [action bar]        │              │
└──────────────────────┴──────────────┘
```

### Lexical Analyzer
```
┌─────────────────────────────────────┐
│  标题 + 说明                         │
└─────────────────────────────────────┘
┌──────────────────┬──────────────────┐
│  Source Code     │  Token Stream    │  ← 1:1 网格
│  [code editor]   │  [table]         │
│  [action bar]    │  [stats]         │
└──────────────────┴──────────────────┘
```

移动端（<900px）自动切换为单列。

## DFA 图渲染规范

使用 Canvas 2D 自定义绘制，不依赖 vis.js：

- 节点圆形排列（圆心在 canvas 中心，半径 = min(W,H) * 0.34）
- 节点半径 26px
- 起始状态：蓝色填充 `#1e3a8a`，蓝色边框 `#3b82f6`，左侧箭头指入
- 接受状态：绿色填充 `#14532d`，绿色边框 `#22c55e`，双圈（内圈间距 4px）
- 普通状态：深灰填充 `#27272a`，灰色边框 `#3f3f46`
- 高亮状态：紫色填充 `#4c1d95`，紫色边框 `#a78bfa`，外发光
- 边：灰色 `#52525b`，箭头三角形，自环用圆弧
- 边标签：等宽字体 10px，灰色，偏移到边的法线方向

## 后续实验扩展指南

每次新实验添加一个 tab 和对应视图：

1. 在 `index.html` 的 `nav-tabs` 里加一个 `<button class="nav-tab" data-view="xxx">`
2. 加一个 `<main id="view-xxx" class="view">` 区块
3. 复用现有的 CSS 类（`.section`、`.card-header`、`.btn`、`.code-input`、`.token-table` 等）
4. JS 里 tab 切换逻辑已经是通用的，不需要改

预期的后续 tab：
- **Lab3**: LR(0) Items — 项目集列表 + 状态转移图
- **Lab4**: SLR(1) Table — ACTION/GOTO 表格
- **Lab5+**: Parse Tree — 树形可视化（可用 Canvas 或 SVG）

## API 集成

前端优先调用服务器 API（C 后端），不可用时降级到内置 JS tokenizer：

```
API_URL = localStorage.getItem('api_url') || 'http://igw.netperf.cc:8080'

POST /api/scan   { "source": "..." }  →  { "tokens": [...] }
POST /api/dfa    { "action": "test", "dfa_file": "...", "input": "..." }
GET  /api/health
```

状态指示器在导航栏右侧：绿色 "API online" / 红色 "Local mode"。

## 文件结构

```
web/                开发目录
  index.html        页面结构
  style.css         样式
  app.js            交互逻辑 + JS tokenizer
docs/               部署目录（从 web/ 复制）
compiler/server.py  API 服务
```

修改 web/ 后执行 `cd compiler && make deploy-web` 同步到 docs/。
