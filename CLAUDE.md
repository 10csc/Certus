# Certus —— 独立通用 AI 研究系统

> 从 WebAISearch 演进而来的独立 Qt 桌面应用。用户通过 GUI 操控浏览器中的 AI 对话平台，获得联网搜索+深度分析能力。

## 项目状态（2026-06-11）

**当前阶段**：第一阶段 —— Python Agent 独立化。

- [x] 项目目录创建，代码从 C 盘开发版复制到 F 盘
- [x] `runtime_paths.py` 改为项目相对路径（不再依赖 `%LOCALAPPDATA%`）
- [x] 4 个平台已定型：deepseek、kimi、chatgpt、gemini
- [x] 架构计划完成（见 [docs/architecture-plan.md](docs/architecture-plan.md)）
- [ ] `orchestrator.execute()` 添加 `on_event` 回调
- [ ] 浏览器管理参数化（接受外部端口而非自启动）
- [ ] 配置来源抽象化
- [ ] C++ Qt 前端开发（CMake + vcpkg）

## 技术栈

| 层 | 技术 |
|----|------|
| 前端 | Qt 6（vcpkg + CMake） |
| 后端 | C++（Qt） |
| 搜索引擎 | Python 3.12（Playwright CDP） |
| 通信 | subprocess stdin/stdout JSON |
| 存储 | SQLite（WAL 模式） |

## 关键文件

- [docs/architecture-plan.md](docs/architecture-plan.md) —— 完整架构计划（361 行，10 轮架构师 QA 产出）
- [docs/session-2026-06-11.jsonl](docs/session-2026-06-11.jsonl) —— 本次对话完整记录（约 28MB）
- `agent/` —— Python Agent 代码（从 WebAISearch 演进）
- `agent/runtime_paths.py` —— 所有路径相对于项目根目录解析
- `config.json` —— 运行时配置（API key、平台 URL、会话链接）

## 与 WebAISearch 的关系

Certus 的 agent/ 目录是 WebAISearch scripts/ 的副本，但：
1. `runtime_paths.py` 已改为项目相对路径
2. 即将删除浏览器自启动逻辑（由 C++ 端管理）
3. 配置源将从 `config.json` 切换到 SQLite
4. 不再有 SKILL.md 或 Claude Code skill 绑定

**开发版仍在 C 盘**：`C:\Users\FANGL\.agents\skills\web-ai-search\scripts\`。两边代码需要手动同步。

## 代码规范

- 所有注释和文档使用中文
- 最小设计原则：每个模块只做一件事，删掉补丁性质代码
- 先跑通再优化，不过早设计复杂结构
