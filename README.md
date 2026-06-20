# Certus —— 独立通用 AI 研究系统

跨 AI 平台低成本搜索复用工具，通过 Playwright CDP 自动化浏览器交互，实现联网搜索结果的缓存和复用。

## 系统要求

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10/11 (x64) |
| 浏览器 | Microsoft Edge 或 Google Chrome |
| Python | 3.12+（需安装 Playwright） |
| 运行时 | Visual C++ 2022 Redistributable |

## 快速开始

### 1. 安装 Python 依赖

```bash
pip install playwright
playwright install chromium
```

### 2. 启动浏览器（CDP 模式）

```bash
# Edge
"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" --remote-debugging-port=9223

# Chrome
"C:\Program Files\Google\Chrome\Application\chrome.exe" --remote-debugging-port=9223
```

### 3. 启动 Certus

```bash
certus_gui.exe
```

在配置页设置 DeepSeek API Key，运行环境检测确认所有依赖就绪，即可开始搜索。

## 构建

```bash
# 需要 Qt 6.5+ (vcpkg) + CMake 3.20+ + MSVC 2022
cmake -B build -S .
cmake --build build --config Release --target certus_gui
# 构建后自动部署到项目根目录
```

## 技术栈

| 层 | 技术 |
|----|------|
| 前端 | Qt 6.5 (C++17, CMake, MSVC 2022) |
| 后端 | C++ (Qt Core/Sql/Network) |
| 搜索引擎 | Python 3.12 (Playwright CDP) |
| 进程通信 | stdin/stdout 4 字节大端长度前缀 JSON 帧 |
| 存储 | SQLite WAL 模式 |
| 加密 | Windows DPAPI (CryptProtectData) |
| 语义缓存 | ChromaDB |

## 架构

```
certus_gui.exe (Qt C++ 前端)
    ├── AgentManager  → Python Agent (agent/agent.py)  ← 搜索编排
    ├── BrowserManager → Edge/Chrome CDP                ← 浏览器生命周期
    ├── Database       → SQLite                         ← 配置与历史
    └── UI Pages       → 配置 / 搜索 / 平台 / 记忆
```

## 许可

MIT License
