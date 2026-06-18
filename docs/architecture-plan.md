# Certus —— 独立通用 AI 研究系统架构计划

> 2026-06-12 架构设计 | 2026-06-18 实现状态标注

## 实现状态速查（2026-06-18）

| 模块 | 状态 | 说明 |
|------|------|------|
| C++ Qt 前端 + Python Agent | ✅ 已实现 | Qt 6.5.3 + Python 3.12 subprocess |
| 长度前缀 JSON 帧协议 | ✅ 已实现 | 4 字节大端 + JsonParser |
| SQLite 存储 | ✅ 已实现 | WAL 模式，C++ 独占写 |
| 配置源统一 | ✅ 已实现 | SQLite 为唯一权威源 |
| 浏览器管理 | ✅ 已实现 | C++ 全权管理，Python external 模式 |
| 平台交互脚本 | ✅ 已实现 | 4 平台 + 通用基类 _base.py |
| 日志系统 | ✅ 已实现 | Logger 类，每日轮转 |
| 状态指示器 | ✅ 已实现 | 五态灯（空闲/搜索/完成/失败） |
| Markdown 渲染 | ✅ 已实现 | GitHub 暗色主题 CSS |
| 搜索历史按项目 | ✅ 已实现 | schema migration 0→1 |
| 数据导出 | ✅ 已实现 | MonitorPage AI 可读 JSON |
| 单实例保护 | ✅ 已实现 | Named Mutex |
| cancel 机制 | ✅ 已实现 | 接入 orchestrator 搜索循环 |
| RepairPage 辅助修复 | ✅ 已实现 | Claude/Codex/DeepSeek 多模型 |
| 五阶段自动进化 | ❌ 已移除 | 不可靠，改手动辅助修复 |
| 自动改代码修复 | ❌ 已移除 | evolution_agent.py → legacy/ |
| 环境自举系统 | 📋 后续 | 战略文档 §2.1 |
| 平台 profile 规范化 | 📋 后续 | 战略文档 §2.2 |
| 发送/提取状态机 | 📋 后续 | 战略文档 §2.3 |
| 成本缓存系统 | 📋 后续 | 战略文档 §2.4 |

> **重要**：本文档为架构设计原文，部分设计（如进化链路 §10.3-10.6、配置双源）已在实际实现中修正。请以上方状态速查和 CLAUDE.md 为准。代码是最终真相源。

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
| **存储** | SQLite（WAL 模式），C++ 独占写入 | Python 不碰 SQLite，所有数据通过 on_event → C++ → SQLite |
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

#### 协议版本与握手

C++ 与 Python 是独立程序，各自独立升级。增加协议版本号防止升级不兼容。

启动握手：
```json
C++ → Python: {"action":"hello","protocol":"1.0","config":{
    "cdp_port":9223,"api_key":"sk-xxx","depth":"L2",
    "search_platform":"deepseek","synthesis_platform":"kimi",
    "data_dir":"F:/CodeFile/Certus/data"}}
Python → C++: {"event":"hello_ack","protocol":"1.0","agent_version":"1.2.3"}
```

- 版本不匹配 → Python 报错退出 → C++ 提示"Agent 版本与主程序不兼容，请更新"
- 向后兼容：C++ 主版本升级可加新字段，不删旧字段。跨主版本不兼容（2.0 ≠ 1.x）

#### 完整事件枚举

```python
# Python → C++ 事件（按搜索生命周期排序）
EVENTS_OUT = [
    "hello_ack",         # 握手响应
    "stage_start",       # 阶段开始（search_1..N / synthesis）
    "stage_progress",    # 可选：阶段进度更新
    "stage_done",        # 阶段完成
    "evolution_start",   # 进化开始（独立进化进程）
    "evolution_done",    # 进化完成
    "evolution_failed",  # 进化失败
    "cancelled",         # 搜索取消确认
    "pong",              # 心跳响应
    "done",              # 搜索完成
    "error",             # 致命错误
]

# C++ → Python 指令
EVENTS_IN = [
    "hello",             # 握手 + 传入配置
    "cancel",            # 取消搜索
    "ping",              # 心跳检测
]
```

#### 通用帧格式

每行 JSON 包含 3 个必填字段 + 可选业务 payload：

```json
{
  "event": "stage_done",          // 必填：事件类型
  "seq": 7,                       // 必填：自增序号（C++ 校验连续性）
  "timestamp": 1718200000.123     // 必填：Unix 时间戳（毫秒精度）
}
```

C++ 容错规则：
- 不识别的 event 类型 → 忽略，不报错（向前兼容）
- 不识别的字段 → 忽略
- `seq` 不连续 → 记录 WARNING 日志，不中断（UDP 风格，允许丢帧）
- 帧完整性：每行 `[4字节大端长度前缀][JSON]`，防止 JSON 内部换行导致拆帧

#### 正常搜索事件流

```json
{"event":"stage_start","seq":1,"timestamp":1718200000.0,"stage":"search_1","question":"Rust trait 设计分析","platform":"deepseek"}
{"event":"stage_done","seq":2,"timestamp":1718200030.5,"stage":"search_1","content_len":5177,"reliability":{"confirmed":8,"reliable":true}}
{"event":"stage_start","seq":3,"timestamp":1718200030.6,"stage":"synthesis","platform":"kimi"}
{"event":"stage_done","seq":4,"timestamp":1718200090.0,"stage":"synthesis","content_len":6232}
{"event":"done","seq":5,"timestamp":1718200090.1,"elapsed_sec":142,"report_path":"F:/CodeFile/Certus/data/latest_result.md"}
```

#### 进化事件流（独立进化进程）

```json
{"event":"evolution_start","seq":1,"timestamp":...,"reason":"上次搜索崩溃，正在分析根因并调整策略..."}
{"event":"evolution_done","seq":2,"timestamp":...,"changes":["deepseek提取超时阈值 30s→45s","fallback选择器增加.content-body"],"fixed":true}
{"event":"evolution_failed","seq":2,"timestamp":...,"error":"无法自动修复，建议手动检查平台状态"}
```

#### C++ → Python 控制指令

```json
{"action":"cancel","seq":1,"timestamp":1718200050.0}   ← 用户点击「取消搜索」
{"action":"ping","seq":2,"timestamp":1718200060.0}      ← C++ 心跳检测（每 30s 一次）
```

Python 收到 `cancel` 后：标记取消，等待当前 AI 回复完成（不中途打断平台），完成后不进入下一阶段，返回已收集内容。

#### Python → C++ 响应

```json
{"event":"cancelled","seq":3,"timestamp":...,"partial_result":"已收集的中间内容摘要"}
{"event":"pong","seq":4,"timestamp":...}
```

#### Python 端线程模型

**三线程隔离**：

```
主线程（搜索）        ← Playwright 同步阻塞（等待 AI 回复）
stdin 监听线程        ← sys.stdin.readline() 阻塞读，接收 cancel/ping
heartbeat 线程        ← 独立回 pong，不做任何阻塞操作
```

- 搜索线程卡死（DOM 阻塞 300s）不影响 heartbeat 回 pong
- heartbeat 超时（35s 无 pong）→ C++ 判定 Python 进程僵死 → 强杀
- stderr 被 C++ 捕获写入日志文件（用户排查需要 traceback）
- Python 所有日志输出走 stderr，不污染 stdout JSON 流

#### on_event 回调 API 签名

```python
def on_event(event_type: str, **payload) -> None:
    """
    搜索过程中推送 JSON 事件到 stdout，供 C++ 读取。
    
    Args:
        event_type: EVENTS_OUT 枚举值（"stage_start"/"stage_done"/"done"/...）
        **payload: 事件可选字段（如 stage="search_1", content_len=5177）
                   自动合并必填字段 seq + timestamp
    
    Implementation:
        1. 全局自增 seq 计数器（线程安全）
        2. 添加 timestamp（time.time() 毫秒精度）
        3. json.dumps() 序列化
        4. 4 字节大端长度前缀 + JSON + '\n'
        5. stdout.buffer.write() + stdout.flush()
    
    同步调用，不阻塞：JSON 序列化 + 写 stdout buffer 是微秒级操作。
    不抛异常：内部 try/except，失败时写 stderr 日志。
    """
```

`orchestrator.execute(on_event=on_event)` 在阶段切换时调用此回调，零侵入现有搜索逻辑。

#### 改造策略

给 `orchestrator.execute()` 加 `on_event` 回调参数。在 agent.py 入口处加 stdin 监听线程 + heartbeat 线程处理 C++ 控制指令。

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
- 报告内嵌 Markdown 渲染（QWebEngine + marked.js，详见下方 4.1.1）
- 右下角状态指示器：
  - ○ 空闲（灰色）
  - ● 正在搜索...（蓝色旋转）
  - ◉ 正在自修复...（橙色旋转）
  - ◎ 搜索完成（绿色，2 秒后变回 ○）
  - ✕ 搜索失败（红色，点击查看详情）

### 4.1.1 报告 Markdown 渲染

**方案**：QWebEngine + marked.js（本地嵌入，不联网）

```javascript
// 安全配置
marked.setOptions({
  sanitize: true,     // 过滤 <script>/<iframe>/onclick
  breaks: true,       // 支持换行
  gfm: true,          // GitHub Flavored Markdown（表格/任务列表）
});
// 代码高亮：highlight.js（本地嵌入）
hljs.highlightAll();
```

| 维度 | 决策 |
|------|------|
| 渲染引擎 | QWebEngineView（独立 Chromium 渲染进程，不阻塞 Qt 主事件循环） |
| Markdown 库 | marked.js（本地嵌入，不联网拉 CDN） |
| 代码高亮 | highlight.js（本地嵌入） |
| 安全 | `sanitize: true` + `LocalContentCanAccessFileUrls = false` |
| 加载方式 | `QUrl::fromLocalFile("data/report.html")`，非 HTTP server |
| 体积代价 | ~80MB（Qt WebEngine），安装包从 ~180MB → ~260MB |

**理由**：研究报告是 Certus 的核心产物，渲染质量优先于安装包体积。260MB 在 2026 年桌面应用中完全可接受。

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
| **启动** | 以 `--remote-debugging-port=N` 启动 Edge/Chrome（扫描 9223-9226，取第一个可用端口） |
| **复用检测** | 扫描端口 9223-9226，已有 CDP 监听且可连接则直接复用 |
| **关闭** | 关闭指定端口的浏览器进程 |
| **防泄露** | `Job Object` 绑定浏览器进程到 C++ 进程树，C++ 崩溃 → OS 自动杀子进程；Job Object 失效时 fallback 杀进程（按 PID） |
| **端口配置** | 持久化在 SQLite 常规配置中，用户可手动指定或选"自动" |

Python 收到 `"browser_ready": {"port": 9223}` 后开始搜索，不再调用 `_launch_browser` / `_kill_browser`。

---

## 六、存储设计

### 6.0 写入策略（硬约束）

**C++ 是 SQLite 的唯一写入者。Python 不碰 SQLite。**

```
Python → on_event → C++ → SQLite
```

| 约束 | 规则 |
|------|------|
| 写入者 | 仅 C++（单线程写，零锁竞争） |
| Python 角色 | 只推事件，不读写 SQLite |
| 配置读取 | C++ 在 hello 握手时通过 stdin 传入配置 dict，Python 不读 SQLite |
| 崩溃安全 | Python 崩溃不留脏 WAL，C++ 崩溃有 WAL 恢复 |

### 6.1 数据保留策略

| 表 | 保留期 | 清理方式 |
|----|--------|---------|
| search_history | 1 年 | 启动时清理过期记录 |
| reliability_snapshot | 1 年 | 随 search_history 级联清理 |
| source_score | 1 年 | 随 search_history 级联清理 |
| platform_perf | 1 年 | 随 search_history 级联清理 |
| evolution_state | 永久 | 手动清理（数据量极小） |
| failure_log | 90 天 | 启动时清理过期记录 |
| config | 永久 | N/A |
| knowledge | 永久 | 用户手动删除 |
| session_state | 单条 | 只保留最新一条 |

### 6.2 SQLite 表结构

```sql
-- 会话状态（防并发 + 崩溃检测）
CREATE TABLE session_state (
    id INTEGER PRIMARY KEY CHECK (id = 1),  -- 永远只有一行
    status TEXT NOT NULL,              -- "idle" | "running" | "evolving"
    started_at TEXT,                   -- 当前搜索开始时间
    pid INTEGER,                       -- Python 进程 PID
    search_query TEXT,                 -- 当前搜索问题（running 时有值）
    updated_at TEXT NOT NULL
);

-- 搜索记录
CREATE TABLE search_history (
    id INTEGER PRIMARY KEY,
    query TEXT, depth TEXT, platform TEXT,
    started_at TEXT, elapsed_sec REAL,
    content_len INTEGER, report_path TEXT,
    status TEXT  -- "done" | "cancelled" | "error"
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
    error_type TEXT,  -- send_failed / extract_failed / cdp_failed / timeout / evolution_failed
    platform TEXT, detail TEXT,
    recorded_at TEXT
);

-- 配置
CREATE TABLE config (
    key TEXT PRIMARY KEY,
    value TEXT,         -- API key 存 DPAPI 密文
    updated_at TEXT
);

-- 记忆
CREATE TABLE knowledge (
    id INTEGER PRIMARY KEY,
    topic TEXT, conclusion TEXT, sources TEXT,
    created_at TEXT
);
```

**外键说明**：`reliability_snapshot.search_id`、`source_score.search_id`、`platform_perf.search_id` 逻辑上引用 `search_history.id`，但不加 FOREIGN KEY 约束。原因：保留策略按时间批量清理（`DELETE ... WHERE recorded_at < ...`），级联删除不如逐条关联表独立清理简单。关联查询走 application 层 JOIN。

### 6.3 事件 → SQL 映射

Python 推事件给 C++，C++ 的 `AgentManager` 解析后调 `Database` 专用写入方法。映射关系：

| Python 事件 | 提取字段 | C++ Database 方法 | 目标表 |
|------------|---------|-------------------|--------|
| `hello_ack` | agent_version | `saveConfig("agent_version", ...)` | config |
| `stage_start` | stage, question, platform | — (仅 UI 展示) | — |
| `stage_done` (搜索阶段) | platform, content_len, reliability | `saveReliability(search_id, platform, ...)` | reliability_snapshot |
| `stage_done` (搜索阶段) | platform, elapsed_sec, success | `savePlatformPerf(platform, "search", ...)` | platform_perf |
| `stage_done` (整合阶段) | platform, content_len, elapsed_sec | `savePlatformPerf(platform, "synthesis", ...)` | platform_perf |
| `stage_done` (含 sources) | sources[] → 逐条 url, category, score | `saveSourceScore(search_id, url, ...)` | source_score |
| `done` | elapsed_sec, report_path | `saveSearchHistory(query, depth, ..., "done")` | search_history |
| `cancelled` | partial_result | `saveSearchHistory(query, depth, ..., "cancelled")` | search_history |
| `error` | error_type, detail | `saveFailureLog(error_type, platform, detail)` | failure_log |
| `error` | error_type, detail | `saveSearchHistory(query, depth, ..., "error")` | search_history |
| `error` (可进化类型) | 完整诊断数据 | `saveFailureLog(..., detail=json.dumps(diagnosis))` | failure_log（进化输入源） |
| `evolution_done` | changes[] → 逐条 platform, key, old, new | `saveEvolutionState(platform, key, ...)` | evolution_state |

**职责边界**：
- `AgentManager`：解析 JSON → 提取字段 → 调用 `Database` 方法
- `Database`：封装所有 SQL 写入，对调用方隐藏表结构
- 搜索开始时 C++ 先 `INSERT INTO search_history` 获取 `search_id`，后续关联表（reliability_snapshot 等）都引用此 id

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
│   │   └── database.h/cpp      ← SQLite 读写（唯一写入者）
│   └── utils/
│       ├── jsonparser.h/cpp    ← Python 事件 JSON 解析（长度前缀帧）
│       └── crypto.h/cpp        ← DPAPI 加密/解密
├── agent/                      ← Python Agent（从 WebAISearch 演进）
│   ├── agent.py                ← 入口：stdin 监听 + heartbeat 线程 + crash 检测
│   ├── orchestrator.py
│   ├── prompt_builder.py
│   ├── extractor.py
│   ├── synthesizer.py
│   ├── planner.py
│   ├── tool_router.py
│   ├── evolution.py
│   ├── diagnostics.py
│   ├── common.py
│   ├── runtime_paths.py
│   ├── platforms/
│   │   ├── deepseek.py
│   │   ├── kimi.py
│   │   ├── chatgpt.py
│   │   └── gemini.py
│   └── tests/
│       ├── test_core.py
│       └── mock_data/          ← Mock 生命周期测试数据
│           ├── search_deepseek_ok.json
│           ├── search_timeout.json
│           ├── search_platform_reject.json
│           ├── extract_fail.json
│           ├── synthesis_kimi_ok.json
│           └── synthesis_empty.json
├── data/                       ← 运行时数据
│   ├── certus.db               ← SQLite 数据库（含 session_state + 全部业务表）
│   ├── latest_result.md        ← 最新搜索报告
│   └── report.html             ← QWebEngineView 加载的报告页面（marked.js 渲染结果）
├── logs/                       ← 滚动日志（每天一个，保留 7 天）
├── config/                     ← 默认配置模板
└── README.md
```

---

## 八、与当前代码的差异要点

基于 WebAISearch 现状，需要做的关键改造：

| 改造项 | 说明 |
|--------|------|
| **on_event 回调** | `execute()` 加 `on_event` 参数，阶段切换时推送事件到 stdout |
| **协议版本化** | Python/C++ 启动握手校验 `protocol_version`，防止独立升级不兼容 |
| **三线程模型** | `agent.py` 入口：主线程（搜索）+ stdin 监听线程 + heartbeat 线程，三者隔离 |
| **浏览器管理剥离** | 删除 `orchestrator` 中自动启动/杀浏览器的逻辑，改为等 C++ 传入 CDP 端口 |
| **每次搜索新进程** | Python 不再驻留，每次搜索 spawn → 运行 → 退出，零状态残留 |
| **配置来源切换** | `common.load_config()` 改为接受 C++ hello 握手传入的 dict，不读 `config.json` |
| **结果路径** | 所有 `data/` 路径改为可配置（从 C++ 传入） |
| **移除 Agent 绑定** | 删除 `SKILL.md` 和 Claude Code 相关文档引用 |
| **入口重构** | 旧 CLI 入口 `main.py`（run_auto/run_send/run_extract）废弃，新入口 `agent.py` → `agent_search()` 从 C++ stdin 读配置 |
| **--mock-cdp 模式** | 跳过 `ensure_browser()`，加载预制 JSON 替代平台回复，测生命周期状态机 |
| **Qt 前端** | 全新开发，不是改造 |

---

## 九、并发与任务管理

### 9.1 设计原则

**串行执行，主动防御**。自动化浏览器操作的本质是放大个人开发者的信息搜索能力，而非让一个用户并行跑多个模型的回复（既不合法也不道德）。

### 9.2 四层防御机制

| 层级 | 机制 | 位置 |
|------|------|------|
| **GUI 层** | 搜索进行中时「开始搜索」按钮灰掉，禁用快捷键触发 | C++ SearchPage |
| **进程层** | `QProcess` 单实例，不允许多个 Python 子进程同时存在 | C++ AgentManager |
| **OS 层** | Windows Named Mutex `Global\CertusSearchMutex`，防多实例并发 | C++ main.cpp 启动时 |
| **数据层** | SQLite `session_state` 表（单行状态），C++ 独占读写 | C++ Database |

### 9.3 Session State 一致性

Named Mutex 和 SQLite session_state 表联合判定（无需额外 JSON 文件）：

```
C++ 启动时：
  ├── Mutex 存在 → 已有搜索在跑 → 提示"搜索进行中" → 拒绝
  └── Mutex 不存在 → 查询 SQLite session_state（WHERE id=1）：
        ├── 表不存在或 status=="idle" → 正常启动
        ├── status=="running" → 上次异常退出 → 触发进化流程
        └── status=="evolving" → 进化也崩了 → 跳过进化，重置为 idle

搜索开始时：
  → C++ 创建 Named Mutex
  → UPDATE session_state SET status='running', pid=..., started_at=...
  
搜索结束时（正常/取消/失败）：
  → C++ 释放 Named Mutex
  → UPDATE session_state SET status='idle'

断电等极端情况：
  → Mutex 被 OS 自动释放
  → session_state 残留 running
  → 下次启动正确判定为异常退出
```

### 9.4 按钮与状态机

```
[空闲] → 用户点击「开始搜索」 → [搜索中（按钮灰）]
[搜索中] → 搜索完成/失败/取消 → [空闲（按钮亮）]
[搜索中] → 用户点击「取消」  → 按钮仍灰（等 Python 确认取消后恢复）

进化期间：
[空闲] → 检测到异常退出 → [自修复中（按钮灰 + 橙色旋转)]
[自修复中] → 进化完成/失败 → [空闲（按钮亮）]
```

### 9.5 右下角状态指示器

| 图标 | 状态 | 说明 |
|------|------|------|
| ○ 灰色 | 空闲 | 可开始新搜索 |
| ● 蓝色旋转 | 搜索中 | Python 进程运行中 |
| ◉ 橙色旋转 | 自修复中 | 进化进程运行中 |
| ◎ 绿色 | 完成 | 搜索成功，2 秒后自动变回灰色 |
| ✕ 红色 | 失败 | 点击查看错误详情 |

---

## 十、异常处理与恢复

### 10.1 分层异常处理

```
C++ AgentManager（进程监控）
  │
  ├── 心跳检测：每 30s 发 {"action":"ping"}，Python 在 5s 内回 {"event":"pong"}
  │     └── 35s 无 pong → 判定 Python 僵死 → 强杀 + 重启（可配置阈值）
  │         注意：heartbeat 线程独立于搜索线程，搜索卡死不影晌心跳
  │
  ├── 子进程崩溃（exit code ≠ 0）：
  │     ├── 读取 stderr 获取崩溃原因（stderr 被 C++ 捕获写入日志）
  │     ├── CDP 连接失败类 → 不进化的致命错误 → 直接报浏览器异常
  │     └── 提取/发送失败类 → 可进化错误 → 标记崩溃，触发进化链
  │
  └── 浏览器进程消失（Job Object 回调 / 端口扫描检测）：
        ├── Job Object 生效 → OS 自动清理
        ├── Job Object 失效 → fallback 按 PID kill
        └── 不等待 Python 报错，主动通知 Python 终止当前操作
```

### 10.2 心跳极端情况

| 场景 | 表现 | 处理 |
|------|------|------|
| AI 回复慢（60-120s） | 搜索线程阻塞等 Playwright | heartbeat 线程独立，正常回 pong |
| Python CPU 100% | GIL 下所有线程阻塞 | 心跳超时 → C++ 强杀 → 重启 |
| Python 僵死（死锁） | 所有线程停止 | 心跳超时 → C++ 强杀 → 重启 |
| stdin 缓冲满 | cancel/ping 发不出 | C++ 超时检测 + 重发一次 + 仍失败则强杀 |

### 10.3 崩溃进化链路（每次搜索新进程模式）

```
搜索进程异常退出（exit ≠ 0）
  → Named Mutex 被 OS 释放
  → session_state 残留 status='running'
  → C++ 检测到异常退出
  → C++ 从 SQLite failure_log 读取最近一次诊断数据（WHERE recorded_at > now - 24h）
       ├── 有有效诊断记录 → 触发进化
       └── 无有效记录 → 跳过进化，直接重置为 idle
  → C++ 更新 session_state → status='evolving'
  → C++ spawn 独立进化进程（mode="evolution"）
  → C++ 通过 hello 握手传入诊断数据：
       {"action":"hello","protocol":"1.0","mode":"evolution",
        "diagnosis":{"error_type":"extract_failed","platform":"deepseek","detail":{...}}}
  → 进化进程（不启动浏览器，不调 Playwright）：
      1. 接收 hello 中的诊断数据
      2. FailureAnalyzer.analyze() 分析根因
      3. StrategyAdapter.adapt() 调整策略
      4. 进化成功 → 推 evolution_done → 退出
      5. 进化失败 → 推 evolution_failed → 退出
  → C++ 前端显示："自修复中..." → "已修复" 或 "需手动处理"
  → C++ 清理：session_state → idle
```

**关键改动**：进化进程不读任何本地文件。诊断数据由搜索进程在崩溃前通过 `on_event("error", ...)` 推给 C++，C++ 存 SQLite failure_log。进化进程启动时，C++ 从 SQLite 加载数据，通过 hello 握手传入。

### 10.4 进化防护

| 防护 | 规则 |
|------|------|
| **超时** | 进化进程 120s 无输出 → C++ 强杀 |
| **次数上限** | 最多连续 2 次进化，第 3 次跳过（防无限循环） |
| **禁止并行** | 进化期间「开始搜索」按钮灰掉，不 spawn 多个进化进程 |
| **诊断校验** | SQLite failure_log 中无 24h 内有效诊断记录 → 跳过进化 |
| **Cancel 优先** | 进化期间用户点取消 → 终止进化 → 重置为 idle |

### 10.5 进化前端展示

搜索页的进度区域直接复用：
- `evolution_start` → 旋转图标 + "检测到上次异常退出，正在自修复..."
- `evolution_done` → 绿色勾 + "已调整：{具体变更}"
- `evolution_failed` → 红色叉 + "自修复失败：{原因}，建议手动检查"

### 10.6 为什么进化由独立 Python 进程执行

- 每次搜索新进程模式下，不存在"Python 重启时自己跑进化"的场景
- C++ 在 spawn 搜索进程前，先检查 session_state → 需要进化则 spawn 独立的进化进程（mode="evolution"）
- 诊断数据来源：搜索进程崩溃前通过 `on_event("error", detail=...)` 推给 C++ → C++ 存 SQLite failure_log → 进化时 C++ 从 SQLite 加载 → 通过 hello 握手传入进化进程
- 进化进程不启动浏览器（不调 Playwright），只分析诊断数据 + 调 LLM API + 修改策略配置
- 进化完成后搜索进程再启动，干净的搜索环境不受进化副作用影响
- 不依赖任何本地 JSON 文件作为中间存储（避免崩溃时的半写文件问题）

---

## 十一、Python Agent 生命周期

### 11.1 模式：每次搜索新进程

**Python 不驻留。每次用户点击「开始搜索」，C++ spawn 一个新 Python 进程，搜索完成后退出。**

| 维度 | 驻留守护进程 | 每次新进程（选择） |
|------|------------|-------------------|
| 内存泄漏 | Playwright 长期驻留积累 | 每次释放，零积累 |
| 状态污染 | cancel_flag、诊断缓存残留 | 全新环境，零污染 |
| 崩溃恢复 | 需检测 + 重启 | 本身就是恢复 |
| 启动开销 | 0 | ~2s（Python 启动 + import） |
| 进程清理 | 需监控驻留进程健康 | 退出即清理 |

**2 秒冷启动完全可接受**。用户点搜索 → 等 2 秒 → 开始。相比 AI 回复等 30-120s，多 2 秒感知不到。

### 11.2 完整生命周期

```
[空闲] 用户点击「开始搜索」
  → C++ 创建 Named Mutex
  → C++ 更新 session_state → status='running'
  → C++ spawn Python 进程（QProcess）
  → C++ 发 hello（含配置 dict）
  → Python 启动（~2s）
  → Python 推 hello_ack
  → Python 执行搜索
  → Python 推 done + 退出
  → C++ QProcess::finished 信号
  → C++ 释放 Named Mutex
  → C++ 更新 session_state → status='idle'
  → C++ 写入 SQLite（搜索记录、可靠性快照等）
  → [空闲]

异常路径：
  → Python crash（exit ≠ 0） / 心跳超时
  → C++ 强杀 QProcess
  → C++ 释放 Named Mutex
  → C++ 记录失败信息到 SQLite
  → C++ 检查是否需要进化（见十、异常处理与恢复）
  → [空闲] 或 [自修复中]
```

### 11.3 Python 退出清理

```python
# agent.py 入口 try/finally 块
def main():
    try:
        agent_search(query, depth, platforms, on_event)
    finally:
        # 1. 关闭 Playwright browser context（断开 CDP，不关浏览器）
        if browser_context:
            browser_context.close()
        # 2. 停止 Playwright
        if playwright:
            playwright.stop()
        # 3. 刷新 stdout（确保所有事件发出）
        sys.stdout.flush()
        # 4. 进程退出，OS 回收所有内存
```

### 11.4 文件组织

注意：第一阶段 `agent.py` 将承担多项职责（stdin 监听、heartbeat、握手、crash 检测、配置解析、Mock CDP、进化入口、退出清理）。若后续超过 300 行，可拆分为：
- `agent_bootstrap.py`：启动握手 + 线程管理 + crash 检测
- `agent_runtime.py`：`agent_search()` 搜索编排

当前保持单文件，因为职责虽多但每个都很短（10-30 行），拆分反而增加模块间传参复杂度。

### 11.5 残留进程保护

| 场景 | 机制 |
|------|------|
| Python 正常退出 | QProcess::finished → 清理 |
| Python 崩溃 | QProcess::error 信号 → C++ 强杀 |
| C++ 崩溃 | Job Object 绑定 → OS 自动杀 Python |
| Python 子进程（Playwright Chromium） | Job Object 级联杀 |
| 浏览器进程 | Job Object 绑定，C++ 崩溃后 OS 自动回收 |

---

## 十二、搜索任务取消

### 12.1 流程

```
用户点击 [取消搜索]
  → C++ 发 {"action":"cancel"} 到 Python stdin
  → C++ 启动 5s 重发计时器
  → Python stdin 监听线程收到 → 设置 cancel_flag = True
  → orchestrator._wait_one() 轮询检查 cancel_flag
       ├── 如果有 AI 回复正在进行 → 等当前回复完成（不中途打断平台）
       └── 当前回复完成后 → 不进入下一阶段 → 返回已收集内容
  → Python 推 {"event":"cancelled","partial_result":"..."}
  → C++ 收到 cancelled → 取消计时器 → 恢复按钮状态

cancel 信号丢失保护：
  → C++ 5s 内未收到 cancelled → 重发一次 cancel
  → 再等 5s 仍无响应 → 判定 Python 僵死 → 走强杀流程
```

### 12.2 设计理由

- **不中途打断平台回复**：CDP 层面打断（Esc 键 / 停止按钮）可能留下脏状态；等待完成只是多等几秒，且不引入平台状态不确定性
- **取消后必须从头开始**：不保存中间状态供恢复。中间状态是浏览器和 AI 平台的联合状态，恢复逻辑复杂且脆弱
- **检测平台是否还在回复**：如果之前已有发送但未等待完成，取消前先完成当前 wait 循环

---

## 十三、平台故障转移

### 13.1 策略

**不做自动切换**。搜索平台和整合平台由用户选择。如果搜索平台高峰期拒绝服务：

```json
{"event":"stage_done","seq":N,"timestamp":...,"stage":"search_1","status":"rejected",
 "error":"高峰期算力不足","suggestion":"try_other_platform"}
```

C++ 前端收到后弹出提示："DeepSeek 当前高峰期，建议更换搜索平台（如 Kimi 或 ChatGPT）后重试。"

### 13.2 理由

- 自动切换会让用户不知道实际用了什么平台，结果来源不透明
- 不同平台的登录态、聊天链接不同，自动切换可能切到未登录平台导致失败
- 用户手动选择保持了透明度和控制权

---

## 十四、API Key 安全设计

### 14.1 加密方案：Windows DPAPI

使用 Windows 原生 `CryptProtectData` API 加密 API key，仅当前用户会话可解密。

### 14.2 数据流

```
用户输入 sk-xxx
  → 正则验证格式（^sk-[a-zA-Z0-9]{32,}$）
  → C++ 端 CryptProtectData 加密
  → SQLite config 表存密文（value 字段）
  → 原始 sk 从内存清除（不落盘）

搜索启动时：
  → C++ 从 SQLite 读密文
  → CryptUnprotectData 解密
  → 通过 Python stdin 传入（{"api_key":"sk-xxx"}）
  → Python 内存中持有，搜索期间使用
  → 搜索完成 → Python 不持久化 → 进程退出即销毁
```

### 14.3 解密失败的容错

DPAPI 绑定 Windows 用户账户。重装系统/换密码/迁移电脑后解密失败：
- C++ 端检测解密失败 → 提示用户"API key 无法解密（系统账户可能已变更），请重新输入"
- 同时存 SHA256 前 8 位做校验哈希，解密后验证
- 用户重新输入 → 重新加密存储

### 14.4 安全边界

- 加密/解密全在 C++ 侧，Python 不接触加密 API
- Python 只在运行期间持有明文 key，进程退出即销毁
- 不经过环境变量（env 可被子进程继承和泄露）
- SQLite 文件中只有密文

---

## 十五、安装分发形态

### 15.1 方案选择：Inno Setup + Python embeddable

```
最终产物：Certus-Setup-1.0.0.exe（~260MB 安装包，含 Qt WebEngine + Python embeddable + Playwright Chromium）

安装包内容：
├── Certus.exe              ← C++ Qt 主程序
├── Qt6*.dll                ← Qt 运行时
├── python/                 ← Python 3.12 embeddable（官方发布，~15MB）
│   └── Lib/site-packages/  ← playright + 依赖（安装时 pip install）
├── agent/                  ← Python Agent 脚本
├── chromium/               ← Playwright Chromium（安装时 playwright install chromium）
├── config/                 ← 默认配置模板
└── data/                   ← 空目录（运行时数据）
```

### 15.2 为什么不选其他方案

| 方案 | 否决原因 |
|------|---------|
| PyInstaller 单 exe | 常被 Windows Defender 误报病毒，用户体验差 |
| 要求用户装 Python | 多一步配置，非技术用户门槛高 |
| Docker | CDP 需要宿主浏览器，容器内跑 headless 无法登录 AI 平台 |

### 15.3 安装脚本流程

```
Inno Setup 安装器：
  1. 解压 Certus.exe + Qt dll + Python embeddable + agent 脚本
  2. 运行 python -m pip install playwright pyyaml
  3. 运行 playwright install chromium（下载 Chromium 到 python/ 旁）
  4. 创建开始菜单快捷方式 + 桌面快捷方式
  5. 可选：写注册表关联 .certus 文件
```

### 15.4 Python 版本升级

- Python embeddable 是官方 ZIP，替换目录即可
- 升级脚本：下载新 Python embeddable → 解压覆盖 → 重新 `pip install` 依赖
- 在「配置管理」页放一个「检查更新」按钮

---

## 十六、日志设计

### 16.1 两层结构

```
┌─ logs/certus.log ───────────────────────
│ 滚动日志文件（每天一个，保留最近 7 天）
│ 格式：[时间] [级别] [模块] 消息
│ 示例：[2026-06-12 14:31:02] [INFO] [search] 搜索开始: Rust trait vs Go interface
│       [2026-06-12 14:32:15] [ERROR] [extractor] 提取超时: deepseek, 45s
│
├─ SQLite failure_log 表 ─────────────────
│ 结构化错误（用于数据监控页故障分析饼图）
│ error_type: send_failed / extract_failed / cdp_failed / timeout / evolution_failed
│ platform, detail, recorded_at
```

### 16.2 日志级别控制

「配置管理」页的滑块控制：

| 级别 | 内容 | 适用场景 |
|------|------|---------|
| ERROR | 异常 + 完整 traceback | 始终记录 |
| WARNING | 重试、降级、平台拒绝 | **默认**，对大多数用户够用 |
| INFO | 搜索开始/结束、阶段切换、进化触发 | 排查流程问题 |
| DEBUG | 完整 CDP 通信、选择器匹配过程 | 开发调试 |

---

## 十七、可测试性

### 17.1 分离策略

| 端 | 测试方式 | 说明 |
|----|---------|------|
| **C++** | 单元测试（Qt Test） | 进程管理、SQLite 读写、JSON 解析都是死逻辑，无外部依赖 |
| **Python 核心逻辑** | 已有 191 个单元测试（pytest） | 核心搜索逻辑已在 WebAISearch 验证，不改逻辑只加 on_event 回调 |
| **Python 生命周期** | Mock CDP 模式测试（pytest） | 测 cancel / timeout / heartbeat / evolution 状态机，不依赖真实浏览器 |
| **集成** | 手动端到端 | 需要真实浏览器 + CDP + AI 平台，不适合 CI |

### 17.2 Mock 测试策略

**Mock Agent 生命周期，不 Mock CDP：**

| 需要真实浏览器 | 测试内容 | Mock 价值 |
|--------------|---------|----------|
| 需要 | DOM 提取、平台交互、选择器匹配 | Mock 没价值（测的是 Mock 自己） |
| **不需要** | **cancel 状态机、超时重试、进化触发、事件推送** | **Mock 有价值** |

Python 侧通过 `--mock-cdp` 参数启动 Mock 模式：跳过 `ensure_browser()` 和 `page.goto()`，用 `agent/tests/mock_data/` 下预制 JSON 替代平台回复，其余状态机逻辑照跑。

```
agent/tests/mock_data/
├── search_deepseek_ok.json       # 正常搜索回复
├── search_timeout.json           # 回复超时
├── search_platform_reject.json   # 平台拒绝（高峰期）
├── extract_fail.json             # 提取失败
├── synthesis_kimi_ok.json        # 正常整合
└── synthesis_empty.json          # 整合平台空回复
```

**Mock 模式隔离**：不写 SQLite session_state，不写 `latest_result.md`，不触发进化。结果写到 `data/_mock_result.md`。

### 17.3 不改核心逻辑

Certus 的 Python Agent 基因来自 WebAISearch（已验证的 191 个测试通过）。改造仅限于：
- `execute()` 加 `on_event` 回调 → 不影响搜索逻辑
- 浏览器管理参数化 → 去掉自启动，改为接受外部端口
- 配置来源抽象化 → 接受 C++ hello 握手传入的 dict
- `agent.py` 入口封装：stdin 监听线程 + heartbeat 线程 + crash 检测

不改动 `extractor`、`prompt_builder`、`synthesizer` 等核心模块。

---

## 十八、实施阶段

### 第一阶段：Python Agent 独立化

**目标**：让 Python Agent 从 WebAISearch Skill 变成可被 C++ 调用的独立进程。

| 任务 | 文件 | 说明 |
|------|------|------|
| `on_event` 回调 | orchestrator.py | `execute(on_event=callback)` → 阶段切换时推 JSON 事件到 stdout |
| 协议帧格式 | agent.py | 每行 `[4字节大端长度][JSON]`，Python 所有日志走 stderr |
| 三线程模型 | agent.py | 主线程（搜索）+ `stdin_listener_thread` + `heartbeat_thread` |
| stdin 监听 | agent.py | `sys.stdin.readline()` 循环，接收 cancel/ping |
| heartbeat 线程 | agent.py | 独立线程回 pong，不阻塞，默认 5s 内响应 |
| 启动握手 | agent.py | 等待 hello → 校验 protocol_version → 回 hello_ack |
| 浏览器参数化 | common.py | `ensure_browser(port=cdp_port)` 接受外部端口，不自启动 |
| 配置抽象化 | common.py | `load_config()` → 接受 C++ hello 握手传入的 config dict |
| Mock CDP 模式 | agent.py | `--mock-cdp` 参数 → 跳过浏览器 → 加载 mock_data/*.json |
| mock_data | agent/tests/mock_data/ | 6 个预制 JSON：正常/超时/拒绝/提取失败/整合OK/整合空 |

### 第二阶段：C++ 后端 + Qt 前端

**目标**：完整的桌面应用骨架，可启动 Python 搜索并显示结果。

| 任务 | 文件 | 说明 |
|------|------|------|
| CMake + vcpkg | CMakeLists.txt | Qt 6 Widgets + WebEngine + Sql |
| 主窗口 + 页面路由 | src/mainwindow.* | 5 页切换（搜索为首页） |
| AgentManager | src/core/agentmanager.* | QProcess spawn/kill + 心跳定时器 + stdin 写指令 + stdout/stderr 读取 |
| BrowserManager | src/core/browsermanager.* | Job Object 绑定 + 端口扫描 9223-9226 + fallback kill |
| Database | src/core/database.* | SQLite WAL + 全部 9 张表 CRUD + 保留策略执行 |
| JSON 解析器 | src/utils/jsonparser.* | 长度前缀帧解码 + seq 校验 + 字段容错 |
| DPAPI 加密 | src/utils/crypto.* | `CryptProtectData`/`CryptUnprotectData` 封装 |
| Named Mutex | src/main.cpp | 启动时创建/检查 `Global\CertusSearchMutex` |
| Session State | src/core/database.* | 启动时读 session_state → 判定是否异常退出 → 触发进化 |
| SearchPage | src/pages/searchpage.* | 输入框 + 深度选择 + 平台选择 + 进度条 + QWebEngineView 报告渲染 |
| MonitorPage | src/pages/monitorpage.* | 5 个子页面：可靠性/信源/性能/进化/故障 |
| ConfigPage | src/pages/configpage.* | 3 个子页面：常规/项目/平台注册 |
| MemoryPage | src/pages/memorypage.* | 结构化知识库 |
| RepairPage | src/pages/repairpage.* | 纯 LLM 对话窗口 |

### 第三阶段：集成 + 打磨

| 任务 | 说明 |
|------|------|
| 端到端测试 | 真实浏览器 + 4 平台 + L2/L3 + cancel + 进化链路 |
| 安装包 | Inno Setup：Certus.exe + Qt dll + Python embeddable + Playwright Chromium + agent 脚本 |
| 启动自检 | C++ main.cpp 启动时：校验 Qt dll 完整性（轻量）。点击「开始搜索」时：校验 Python 版本 + Playwright + Chromium 可用性（搜索需要时才检） |
| 文档 | README + 用户手册 + 开发文档 |
