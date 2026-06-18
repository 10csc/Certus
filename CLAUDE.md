# Certus —— 跨 AI 平台低成本搜索复用系统

> 从 WebAISearch 演进而来的独立 Qt 桌面应用。核心定位：让个人用户稳定复用各 AI 平台的联网搜索能力，通过缓存和复用减少 token 成本。

## 项目状态（2026-06-18）

**当前阶段**：稳定化 —— 主链路已硬，减肥增壮中。

- [x] C++ Qt 前端 + Python Agent 后端完整架构
- [x] 4 个平台已定型：deepseek、kimi、chatgpt、gemini
- [x] 平台交互通用基类 `agent/platforms/_base.py`（4 个平台脚本共享）
- [x] SQLite 为唯一运行时配置权威源（config.json 降级为 CLI 兼容）
- [x] C++ hello 传完整 runtime config（Python Protocol 模式零 config.json 依赖）
- [x] C++ 全权管理浏览器生命周期（Python launch_policy="external"）
- [x] cancel 机制接入 orchestrator 搜索循环（_wait_one + L3 循环）
- [x] RepairPage 辅助修复（Claude/Codex/DeepSeek 多模型，支持文件上下文）
- [x] Logger 日志系统（每日轮转 + 自动清理）
- [x] 搜索历史按项目隔离（schema migration 0→1）
- [x] MonitorPage 数据导出（AI 可读 JSON）
- [x] 单实例保护（Global\CertusSingleInstance Mutex）
- [x] 自动进化/自修复已移除（不可靠，改为手动辅助修复）
- [x] evolution_agent.py 移至 legacy/（参考实验工具）

## 技术栈

| 层 | 技术 |
|----|------|
| 前端 | Qt 6.5.3（vcpkg + CMake + MSVC 2022） |
| 后端 | C++（Qt） |
| 搜索引擎 | Python 3.12（Playwright CDP） |
| 通信 | subprocess stdin/stdout，4 字节大端长度前缀 JSON 帧协议 |
| 存储 | SQLite WAL 模式（C++ 独占写，Python 不接触） |
| 加密 | Windows DPAPI（CryptProtectData/CryptUnprotectData） |

## 配置流（唯一权威源）

```
ConfigPage 保存 → SQLite config 表
                 ↓
AgentManager::start() → hello JSON（从 SQLite 动态组装全字段 + local_env）
                 ↓
Python load_config() → dict(runtime_config)（Protocol 模式不触碰 config.json）
```

## 关键文件

| 文件 | 用途 |
|------|------|
| [docs/architecture-plan.md](docs/architecture-plan.md) | 完整架构设计（决策记录） |
| [docs/issues-log.md](docs/issues-log.md) | 项目问题日志与经验总结 |
| `agent/agent.py` | Python Agent 主入口 |
| `agent/orchestrator.py` | 搜索编排层（send → wait → extract → synthesize） |
| `agent/platforms/_base.py` | 平台交互通用基类（输入框查找/发送/消除弹窗） |
| `agent/common.py` | 通用配置、浏览器连接、API 调用 |
| `agent/evolution.py` | 平台 profile 自适应（纯规则引擎，无 LLM 调用） |
| `agent/synthesizer.py` | 交叉验证 + 报告生成 |
| `src/core/agentmanager.cpp` | Agent 进程管理与协议通信 |
| `src/core/database.cpp` | SQLite 封装（所有写入统一走此类） |
| `src/core/browsermanager.cpp` | 浏览器生命周期管理 |
| `src/pages/searchpage.cpp` | 搜索页 UI |
| `src/pages/configpage.cpp` | 配置页 UI（SQLite 加载，不再写 config.json） |
| `src/pages/repairpage.cpp` | 辅助修复页（多模型 AI 诊断） |
| `src/utils/logger.cpp` | 日志系统 |

## 代码规范

- 所有注释和文档使用中文
- 最小设计原则：每个模块只做一件事，删掉补丁性质代码
- SQLite 操作统一走 `Database` 封装类，禁止直接 SQL 拼接
- 平台交互脚本统一基于 `_base.py`，不重复造轮子
- 新功能四条准入标准：可观测、可验证、可回滚、低成本

## 已移除的设计（避免 AI 误判）

- ~~自动进化/自修复~~ — 不可靠，已移除。evolution_agent.py 在 legacy/
- ~~config.json 双源写入~~ — ConfigPage 不再写 config.json
- ~~C++ hello 硬编码 platform_urls~~ — 改为 SQLite 动态读取
- ~~AgentManager 直接 SQL~~ — 统一走 Database 方法
- ~~Python load_config config.json fallback~~ — Protocol 模式零接触
