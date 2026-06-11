# Certus —— 独立通用 AI 研究系统架构计划

> 2026-06-11 | 基于 10 轮架构师 QA 完整记录

---

## 一、项目定位

**Certus**（拉丁语"确定/已验证"）是一个独立的通用 AI 研究搜索系统。基因来自 WebAISearch
的"浏览器 CDP → AI 平台 → 交叉验证 → 研究报告"核心技术链，但不再绑定 Claude Code
等代码 Agent，向通用研究工具发展。

**与 WebAISearch 的关系**：

| 维度 | WebAISearch | Certus |
|------|------------|--------|
| 定位 | Claude Code skill，提升 AI 编程助手的搜索能力 | 独立通用研究系统，谁都能用 |
| 入口 | Agent 通过 CLI 调用 | Qt 桌面应用，用户直接操作 |
| 绑定 | 依赖 Claude Code skill 体系 | 零绑定，独立的 exe |
| 配置 | JSON 文件手动编辑 | GUI 可视化配置 |
| 监控 | 无 | SQLite + 饼图/趋势线 |
| 浏览器管理 | Python 内部自动管理 | C++ GUI 按钮控制 |

---

## 二、技术栈

| 层 | 技术 | 理由 |
|----|------|------|
| **前端** | Qt 6（vcpkg + CMake，~几十 MB） | C++ 同语言、原生控件、后续系统级监控无桥接开销 |
| **后端** | C++（Qt） | 进程管理、浏览器生命周期、SQLite 操作 |
| **搜索引擎** | Python 3.12（Playwright CDP） | 复用 WebAISearch 核心，独立进程 |
| **通信** | C++ subprocess → Python stdin/stdout JSON | 方案 A（当前）+ 预留方案 C 接口（pybind11） |
| **存储** | SQLite（WAL 模式） | Qt 原生 `QSqlDatabase` + Python 端直接读写 |
| **浏览器** | Edge/Chrome CDP | C++ Job Object 管理生命周期，防进程泄露 |

---

## 三、系统架构

```
┌─────────────────────────────────────────────────┐
│                  Qt 桌面应用                      │
│                                                  │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐         │
│  │ 会话搜索  │ │ 数据监控  │ │ 配置管理  │         │
│  │ (主页面)  │ │ (二级页)  │ │ (二级页)  │         │
│  └────┬─────┘ └────┬─────┘ └────┬─────┘         │
│       │             │             │               │
│  ┌────┴─────────────┴─────────────┴────┐         │
│  │           C++ 后端核心               │         │
│  │  ┌──────────┐ ┌──────────┐          │         │
│  │  │ 进程管理  │ │ SQLite   │          │         │
│  │  │ (JobObj)  │ │ 读写层   │          │         │
│  │  └────┬─────┘ └────┬─────┘          │         │
│  │       │             │                 │         │
│  │  ┌────┴─────────────┴────┐           │         │
│  │  │   subprocess stdin/   │           │         │
│  │  │   stdout JSON 通信    │           │         │
│  │  └──────────┬───────────┘           │         │
│  └─────────────┼───────────────────────┘         │
│                │                                  │
└────────────────┼──────────────────────────────────┘
                 │
    ┌────────────┴────────────┐
    │   Python Agent 进程     │
    │                        │
    │  agent_search(query)    │
    │  → router → planner    │
    │  → orchestrator         │
    │  → extractor            │
    │  → synthesizer          │
    │                        │
    │  事件推送: on_event()   │
    │  → stdout JSON          │
    └────────────────────────┘
```

### 通信协议

Python 进程通过 stdout 推送 JSON 事件，C++ 逐行读取：

```json
{"event":"stage_start","stage":"search_1","question":"Rust trait 设计分析","platform":"deepseek"}
{"event":"stage_done","stage":"search_1","content_len":5177,"reliability":{"confirmed":8,"reliable":true}}
{"event":"stage_start","stage":"synthesis","platform":"kimi"}
{"event":"stage_done","stage":"synthesis","content_len":6232}
{"event":"done","elapsed_sec":142,"report_path":"F:/Certus/data/latest_result.md"}
```

改造策略：给 `orchestrator.execute()` 加 `on_event` 回调参数，零侵入现有逻辑。

---

## 四、页面设计

### 4.1 会话搜索（主页面）

用户日常使用的主界面，输入问题 → 看到进度 → 得到报告。

```
┌─────────────────────────────────────────┐
│  🔍 搜索问题                              │
│  ┌─────────────────────────────────────┐ │
│  │ Rust trait 和 Go interface 的设计    │ │
│  │ 差异及选型建议                        │ │
│  └─────────────────────────────────────┘ │
│                                          │
│  深度: ○ L2 标准  ● L3 深度研究           │
│  搜索平台: [DeepSeek ▼]  整合平台: [Kimi ▼]│
│                                          │
│  [开始搜索]                               │
│                                          │
│  ┌─ 搜索进度 ──────────────────────────┐ │
│  │ ● 方向 1/2: Rust trait 设计分析       │ │
│  │   ✓ 5177 字符 | 可靠 (8确认)          │ │
│  │ ○ 方向 2/2: Go interface 设计分析     │ │
│  │   等待中...                          │ │
│  │ ○ 整合验证                            │ │
│  └──────────────────────────────────────┘ │
│                                          │
│  ┌─ 最终报告 ──────────────────────────┐ │
│  │ # Rust trait vs Go interface         │ │
│  │ 核心结论...                           │ │
│  │ [展开全文]                            │ │
│  └──────────────────────────────────────┘ │
│                                          │
│  ┌─ 历史记录 ──────────────────────────┐ │
│  │ 🔍 [搜索框]                           │ │
│  │ 06-11  Rust trait vs Go interface    │ │
│  │ 06-10  微服务架构选型                  │ │
│  │ 06-09  Python asyncio 最佳实践        │ │
│  └──────────────────────────────────────┘ │
└─────────────────────────────────────────┘
```

**特点**：
- 半黑盒进度——只显示当前阶段，详细信息用户去浏览器看
- 历史记录如聊天记录翻页，搜索框做匹配（无渲染，避免卡死）
- 报告内嵌 Markdown 渲染

### 4.2 数据监控（二级页面切换）

长期积累的统计数据，帮助用户了解系统运行质量和平台表现。

```
┌─ 数据监控 ──────────────────────────────────┐
│ [可靠性总览] [信源分布] [平台性能] [进化状态] [故障分析] │
│                                              │
│  ┌─ 可靠性指标趋势（近30天）────────────────┐ │
│  │  ████░░░░░░░░  已确认: 67%               │ │
│  │  ██░░░░░░░░░░  推断:   22%               │ │
│  │  █░░░░░░░░░░░  未确认: 11%               │ │
│  │  [折线图: 每日可靠性比例变化]              │ │
│  └──────────────────────────────────────────┘ │
│                                              │
│  ┌─ 信源评分分布 ──────────────────────────┐ │
│  │  [饼图: 官方 45% | 学术 20% | 社区 15%  │ │
│  │         媒体 12% | 自媒体 8%]            │ │
│  └──────────────────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

**五个子页面**：
- **可靠性总览**：已确认/推断/未确认比例趋势、平均可信度评分
- **信源分布**：信源类别饼图、各平台信源质量对比
- **平台性能**：响应时延、成功率、重搜次数、各平台对比表
- **进化状态**：各平台阈值变化历史（如稳定阈值从 3→5 的过程）
- **故障分析**：失败次数 + 失败原因饼图（CDP 不可用/发送超时/提取失败/平台拒绝）

### 4.3 配置管理（二级页面切换）

```
┌─ 配置 ──────────────────────────────────────┐
│ [常规配置] [项目管理] [平台注册]               │
│                                              │
│  ┌─ 常规配置 ──────────────────────────────┐ │
│  │ CDP 端口: [9223]                         │ │
│  │ API Key:   [sk-●●●●●●●●]    [测试连接]   │ │
│  │                                          │ │
│  │ 自动分析问题等级: [开启 ☑]                 │ │
│  │   默认深度: ○ L1  ● L2  ○ L3              │ │
│  │   开启后系统自动判断深度，关闭后允许手动设置  │ │
│  └──────────────────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

**三个子页面**：
- **常规配置**（默认页、小白友好）：CDP 端口、API key、自动分析开关
- **项目管理**（项目 > 平台 > 聊天链接）：项目路径绑定、平台链接增删改、异常提醒
- **平台注册**（高级）：新平台模板注册 + LLM 辅助定型、已有平台编辑

### 4.4 记忆管理

简单结构化知识库。用户手动标记有价值的搜索结论 → 按主题索引存储 → 可检索回顾。

### 4.5 辅助修复

独立 LLM 对话窗口，与搜索共用 API key。用户输入错误信息 → LLM 诊断 → 修复建议。本质是绕过搜索流程的纯 LLM 对话，给自进化兜底。

---

## 五、浏览器生命周期管理

C++ 全权管理，Python 不拥有启动/杀死浏览器的决策权：

| 操作 | C++ 行为 |
|------|---------|
| **启动** | 以 `--remote-debugging-port=N` 启动 Edge/Chrome |
| **复用检测** | 扫描端口，已有 CDP 则直接复用 |
| **关闭** | 关闭指定端口的浏览器进程 |
| **防泄露** | `Job Object` 绑定浏览器进程到 C++ 进程树，C++ 崩溃 → OS 自动杀子进程 |
| **端口配置** | 持久化在 SQLite 常规配置中 |

Python 收到 `"browser_ready": {"port": 9223}` 后开始搜索，不再调用 `_launch_browser` / `_kill_browser`。

---

## 六、存储设计

### SQLite 表结构（草案）

```sql
-- 搜索记录
CREATE TABLE search_history (
    id INTEGER PRIMARY KEY,
    query TEXT, depth TEXT, platform TEXT,
    started_at TEXT, elapsed_sec REAL,
    content_len INTEGER, report_path TEXT
);

-- 可靠性快照
CREATE TABLE reliability_snapshot (
    id INTEGER PRIMARY KEY,
    search_id INTEGER, platform TEXT,
    confirmed INTEGER, inferred INTEGER, unconfirmed INTEGER,
    recorded_at TEXT
);

-- 信源评分
CREATE TABLE source_score (
    id INTEGER PRIMARY KEY,
    search_id INTEGER, url TEXT, category TEXT, score INTEGER
);

-- 平台性能
CREATE TABLE platform_perf (
    id INTEGER PRIMARY KEY,
    platform TEXT, stage TEXT,  -- search / synthesis
    elapsed_sec REAL, success INTEGER,
    recorded_at TEXT
);

-- 进化状态
CREATE TABLE evolution_state (
    id INTEGER PRIMARY KEY,
    platform TEXT, key TEXT, old_value TEXT, new_value TEXT,
    changed_at TEXT
);

-- 失败记录
CREATE TABLE failure_log (
    id INTEGER PRIMARY KEY,
    error_type TEXT,  -- send_failed / extract_failed / cdp_failed / ...
    platform TEXT, detail TEXT,
    recorded_at TEXT
);

-- 配置
CREATE TABLE config (
    key TEXT PRIMARY KEY,
    value TEXT,
    updated_at TEXT
);

-- 记忆
CREATE TABLE knowledge (
    id INTEGER PRIMARY KEY,
    topic TEXT, conclusion TEXT, sources TEXT,
    created_at TEXT
);
```

---

## 七、目录结构

```
F:/CodeFile/Certus/
├── CMakeLists.txt              ← Qt C++ 构建
├── src/                        ← C++ 源码
│   ├── main.cpp
│   ├── mainwindow.h/cpp        ← 主窗口（页面路由）
│   ├── pages/
│   │   ├── searchpage.h/cpp    ← 会话搜索
│   │   ├── monitorpage.h/cpp   ← 数据监控
│   │   ├── configpage.h/cpp    ← 配置管理
│   │   ├── memorypage.h/cpp    ← 记忆管理
│   │   └── repairpage.h/cpp    ← 辅助修复
│   ├── core/
│   │   ├── agentmanager.h/cpp  ← Python 进程管理
│   │   ├── browsermanager.h/cpp← CDP 浏览器管理
│   │   └── database.h/cpp      ← SQLite 读写
│   └── utils/
│       └── jsonparser.h/cpp    ← Python 事件 JSON 解析
├── agent/                      ← Python Agent（从 WebAISearch 演进）
│   ├── agent.py
│   ├── orchestrator.py
│   ├── prompt_builder.py
│   ├── extractor.py
│   ├── synthesizer.py
│   ├── planner.py
│   ├── tool_router.py
│   ├── evolution.py
│   ├── common.py
│   ├── diagnostics.py
│   ├── runtime_paths.py
│   └── platforms/
│       ├── deepseek.py
│       ├── kimi.py
│       ├── chatgpt.py
│       └── gemini.py
├── data/                       ← 运行时数据（SQLite + 报告）
├── config/                     ← 默认配置模板
└── README.md
```

---

## 八、与当前代码的差异要点

基于 WebAISearch 现状，需要做的关键改造：

| 改造项 | 说明 |
|--------|------|
| **on_event 回调** | `execute()` 加 `on_event` 参数，阶段切换时推送事件到 stdout |
| **浏览器管理剥离** | 删除 `orchestrator` 中自动启动/杀浏览器的逻辑，改为等 C++ 给信号 |
| **配置源切换** | `common.load_config()` 从读 SQLite（C++ 写入），不再读 `config.json` |
| **结果路径** | 所有 `data/` 路径改为可配置（从 C++ 传入） |
| **移除 Agent 绑定** | 删除 `SKILL.md` 和 Claude Code 相关文档引用 |
| **Qt 前端** | 全新开发，不是改造 |

---

## 九、实施阶段

### 第一阶段：Python Agent 独立化（当前可做）
- `execute()` 添加 `on_event` 回调
- 浏览器管理参数化（接受外部端口而非自启动）
- 配置来源抽象化（接口而非 JSON 文件）

### 第二阶段：C++ 后端 + Qt 前端（核心开发）
- CMake + vcpkg 搭建 Qt 工程
- Python 进程管理 + Job Object
- SQLite schema + 读写层
- 5 个页面开发

### 第三阶段：集成 + 打磨
- 端到端测试
- 性能优化
- 安装打包（CPack / WiX）
