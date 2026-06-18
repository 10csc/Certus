# Certus —— 独立通用 AI 研究系统

> 从 WebAISearch 演进而来的独立 Qt 桌面应用。用户通过 GUI 操控浏览器中的 AI 对话平台，获得联网搜索+深度分析能力。

## 项目状态（2026-06-18）

**当前阶段**：第二阶段 —— 稳定化与审计修复。

- [x] 项目目录创建，代码从 C 盘开发版复制到 F 盘
- [x] `runtime_paths.py` 改为项目相对路径（不再依赖 `%LOCALAPPDATA%`）
- [x] 4 个平台已定型：deepseek、kimi、chatgpt、gemini
- [x] 架构计划完成（见 [docs/architecture-plan.md](docs/architecture-plan.md)）
- [x] `orchestrator.execute()` 添加 `on_event` 回调
- [x] 浏览器管理参数化（`launch_policy="external"`，C++ 全权管理）
- [x] 配置源统一化（SQLite 为唯一权威源，config.json 降级兼容）
- [x] C++ Qt 前端开发（CMake + vcpkg）
- [x] 平台交互脚本通用基类 `_base.py`
- [x] cancel 机制接入 orchestrator 搜索循环
- [x] 独立 RepairPage（Claude/Codex/DeepSeek 多模型代码诊断）
- [x] 自进化流程已移除（自动修改代码不可靠，改为手动辅助修复）

## 技术栈

| 层 | 技术 |
|----|------|
| 前端 | Qt 6.5.3（vcpkg + CMake + MSVC 2022） |
| 后端 | C++（Qt） |
| 搜索引擎 | Python 3.12（Playwright CDP） |
| 通信 | subprocess stdin/stdout JSON 帧协议（4字节大端长度前缀） |
| 存储 | SQLite WAL 模式（C++ 独占写，Python 不接触） |
| 加密 | Windows DPAPI（CryptProtectData/CryptUnprotectData） |

## 关键文件

- [docs/architecture-plan.md](docs/architecture-plan.md) —— 完整架构计划
- [docs/issues-log.md](docs/issues-log.md) —— 项目问题日志与经验总结
- `agent/` —— Python Agent 代码
- `agent/platforms/_base.py` —— 平台交互通用基类
- `agent/orchestrator.py` —— 搜索编排层（支持 cancel 信号）
- `src/` —— C++ Qt 前端
- `src/core/agentmanager.cpp` —— Agent 进程管理与通信
- `src/pages/repairpage.cpp` —— 辅助修复页（多模型 AI 诊断）
- `src/utils/logger.cpp` —— 日志系统（每日轮转 + 自动清理）
- `config.json` —— 运行时配置（CLI 模式和向后兼容）

## 配置流

```
用户配置 (ConfigPage)
    ↓ 写
SQLite config 表（唯一权威源）
    ↓ C++ AgentManager::start() 动态读取
hello JSON → Python set_runtime_config()
    ↓ load_config()
runtime_config 为基座，config.json 仅补充缺失字段
```

## 与 WebAISearch 的关系

Certus 的 agent/ 目录从 WebAISearch scripts/ 演进而来：
1. `runtime_paths.py` 已改为项目相对路径
2. 浏览器全权由 C++ 端管理（`launch_policy="external"`）
3. 配置源已从 `config.json` 切换到 SQLite
4. 不再有 SKILL.md 或 Claude Code skill 绑定

**开发版仍在 C 盘**：`C:\Users\FANGL\.agents\skills\web-ai-search\scripts\`。两边代码需要手动同步。

## 代码规范

- 所有注释和文档使用中文
- 最小设计原则：每个模块只做一件事，删掉补丁性质代码
- 先跑通再优化，不过早设计复杂结构
- SQLite 操作统一走 Database 封装类，禁止直接 SQL 拼接
