# Certus 项目问题日志 —— 踩坑记录与经验总结

> 2026-06-14 | 记录从 WebAISearch 演进到 Certus 过程中遇到的所有关键问题、根因及解决方案

---

## 一、架构与设计类

### 1.1 平台交互脚本缺少通用基类（"举一反三"问题）

**现象**：ChatGPT 输入框误选 AI 回复编辑区 → 修复 ChatGPT → DeepSeek/Kimi/Gemini 仍然用 `.first` 直接取第一个元素，存在同样隐患。

**根因**：4 个平台脚本各自独立实现 `fill_prompt` / `submit` / `dismiss_blockers`，逻辑重复且不一致。修一个平台不惠及其他平台。

**解决**：创建 `agent/platforms/_base.py` 通用基类，提供：
- `is_inside_message()` —— 遍历父节点检测是否在 AI 回复容器内
- `find_input_box()` —— 5 策略查找输入框（先自定义 → contenteditable 过滤 → textarea 过滤 → data-placeholder → Tab）
- `find_submit_button()` —— 3 策略查找发送按钮（自定义 → 通用过滤 → form svg）
- `safe_fill()` / `safe_submit()` —— 完整填入/提交流程
- `dismiss_blockers_base()` —— 通用弹窗消除

四个平台脚本全部重构为调用 `_base.py`，只保留平台专用选择器和特殊行为（如 Kimi 的 React contenteditable 键盘处理）。

**教训**：同类模块变多时，立刻抽取公共逻辑。宁可早做抽象，不要让 Bug 在不同副本中反复出现。

---

### 1.2 自动进化/自修复流程不可用

**现象**：自修复流程启动后几乎没有进度，不仅无法完成修复，还在搜索失败后造成额外干扰。

**根因**：LLM 驱动的自动代码修复需要精确的上下文理解、文件定位和修改验证。在进程中自动触发（崩溃后立刻启动 `evolution_agent.py`）缺乏人工判断环节，质量不可控。

**解决**：完全移除自动进化流程：
- 删除 `AgentManager::startEvolution()` 及五阶段信号
- 删除 `SearchPage` 中的进化 UI
- 保留 `checkEvolution()` 但仅记录日志 + 提示用户前往「辅助修复」页
- 升级 RepairPage 为多模型（Claude Opus/Sonnet/Haiku + Codex + DeepSeek）手动诊断工具

**教训**：自动化代码修复是 AI 的最难任务之一。在没有严格测试保护的情况下，自动修改代码弊大于利。应该提供工具辅助而非直接操作。

---

### 1.3 搜索历史缺少项目维度

**现象**：所有搜索记录混在一起，无法按项目区分。

**根因**：数据库 `search_history` 表没有 `project` 字段，`recentSearches()` 查询不支持项目过滤。

**解决**：
- SQLite schema migration（`PRAGMA user_version` 0→1）：`ALTER TABLE search_history ADD COLUMN project TEXT DEFAULT ''`
- `recentSearches()` 添加 `project` 参数
- `SearchPage::refreshHistory()` 读取 `current_project` 按项目过滤

**教训**：多项目管理是研究系统的核心需求，应在初期设计 schema 时就考虑。

---

## 二、通信协议类

### 2.1 Silent Failure —— 管道断裂被静默吞下

**现象**：Python Agent 崩溃后，C++ 端的 `writeToStdin` 写入失败但无任何错误信号，C++ 端等到心跳超时才发觉。

**根因**：`agent_protocol.py` 中 `write_frame()` 捕获异常后仅 `return`，不设置任何错误标志；`read_frame()` 遇到损坏帧也仅 `return`。

**解决**：
- `write_frame()` 失败时设置 `pipe_broken = threading.Event()`
- `read_frame()` 遇到损坏帧调用 `sys.exit(1)` 而非静默返回
- 新增 `stdin_thread_died = threading.Event()` 检测 stdin 监听线程退出
- 三个搜索线程定期检查 `pipe_broken` 和 `stdin_thread_died`

**教训**：进程间通信的每一处失败路径都必须显式传播错误。Silent failure 是最难排查的 Bug。

### 2.2 多实例并发保护缺失

**现象**：用户可以在旧程序未退出时启动新程序，两个进程同时操控浏览器，造成干扰。

**根因**：没有单实例保护机制。

**解决**：
- `main_gui.cpp` 启动时创建 Windows Named Mutex `Global\CertusSingleInstance`
- `AgentManager` 搜索时获取 `Global\CertusSearchMutex` 防止并发搜索
- `CreateMutexW` 失败返回 `false`（原代码返回 `true` 并注释"非致命"）

**教训**：桌面应用必须处理多实例。Named Mutex 是 Windows 上的标准做法。

---

## 三、浏览器管理类

### 3.1 CDP 端口配置不自动保存

**现象**：用户在配置页输入端口号后点启动浏览器，端口号未保存，下次打开还是旧值。

**根因**：`onLaunchBrowser()` 先启动浏览器再保存配置，启动失败时 save 不执行。

**解决**：`onLaunchBrowser()` 先调用 `onSaveConfig()` 保存，再启动浏览器。

**教训**：任何用户操作修改的配置都应在操作执行前持久化，确保数据不丢失。

### 3.2 浏览器冷启动超时

**现象**：浏览器启动后窗口可见，但 GUI 报告"启动失败"。

**根因**：CDP 等待时间仅 10s，浏览器冷启动（含扩展加载）可能超过此时间。

**解决**：CDP 等待增加到 20s，超时后检查进程是否存活并给出诊断提示。

**教训**：超时值应根据目标系统的实际性能设置，并给出有意义的错误信息而非仅"失败"。

### 3.3 搜索页不自动启动浏览器

**现象**：用户需要在配置页手动启动浏览器才能搜索，搜索页点击"开始搜索"只报错。

**根因**：搜索流程缺少热启动逻辑。

**解决**：`onStartSearch()` 检测 CDP 不可用时自动调用 `m_browser->launch()` 热启动。

**教训**：用户操作的常见路径应该有自动化的 fallback，减少手动步骤。

---

## 四、UI 与状态管理类

### 4.1 状态标签显示矛盾信息

**现象**：搜索页同时显示"浏览器未配置"和"配置就绪"。

**根因**：两个独立标签（`m_browserStatus` 和 `m_configStatus`）各自更新，没有交叉校验。

**解决**：合并浏览器状态到配置完整性评估。`checkConfigStatus()` 中一次 CDP 扫描结果同时用于浏览器标签和配置状态。

**教训**：多个状态指示器必须共享同一个数据源或交叉验证，否则会出现逻辑矛盾。

### 4.2 配置页修改后搜索页不同步

**现象**：在配置页改了搜索平台，切回搜索页仍显示旧平台。

**根因**：搜索页只在初始化时读取平台配置，之后不更新。

**解决**：`checkConfigStatus()` 每 5s 从 SQLite 同步最新配置到只读标签。MainWindow 切页时主动调用 `checkConfigStatus()`。

**教训**：跨页面的配置同步应当用定时器或信号机制保证一致性。

### 4.3 Markdown 报告渲染简陋

**现象**：搜索结果报告只有粗体/斜体，表格、代码块、引用等全部丢失。

**根因**：`markdownToHtml()` 只实现了个别标记。

**解决**：增强解析器支持表格、代码块（含语言标注）、引用块、分割线、行内代码、删除线、任务列表、嵌套列表。采用 GitHub 风格暗色主题 CSS。

**教训**：报告渲染是研究系统的核心体验，应在 MVP 阶段就做到基本完整。

---

## 五、平台选择器类

### 5.1 ChatGPT 输入框误选 AI 回复编辑区

**现象**：切换到 ChatGPT 平台后搜索卡死，第一步都无法进行。Agent 将 GPT 某个 AI 回复的 contenteditable 当成了输入框。

**根因**：GPT 的 AI 回复也是 `div[contenteditable="true"]`。用 `.first` 取第一个匹配元素时命中了页面中部某条 AI 回复的编辑区，而非底部的真正输入框。

**解决**：
- `_is_inside_message()` 函数：向上遍历 12 层父节点，检测 `className` 是否含 `message`/`response`/`conversation`/`agent-turn`/`model-turn`/`assistant`，或 `role` 为 `article`/`listitem`
- `find_input_box()` 改为从最后一个元素往前找（`prefer_last=True`），输入框通常在页面底部
- ChatGPT 专用精确选择器 `#prompt-textarea` 作为最高优先级

**教训**：AI 对话平台的 DOM 结构与传统 Web 应用不同——AI 回复区也可能含可编辑元素。通用的"取第一个"策略必须改为"取最后一个且不在消息容器内"。

---

## 六、编译与构建类

### 6.1 链接错误 —— 进程占用

**现象**：`LNK1104: cannot open certus_gui.exe`。

**根因**：GUI 进程仍在运行，MSBuild 无法覆盖 exe。

**解决**：编译前执行 `taskkill /F /IM certus_gui.exe`。

**教训**：构建脚本应包含自动杀旧进程的逻辑。

### 6.2 变量名冲突 —— Most Vexing Parse

**现象**：`QNetworkRequest req(QUrl(apiUrl))` 被 MSVC 解析为函数声明。

**根因**：C++ 的 Most Vexing Parse 规则——当声明可被解释为函数原型时，编译器优先解释为函数。

**解决**：使用两步构造：`QNetworkRequest netReq; netReq.setUrl(QUrl(apiUrl));`

**教训**：在 C++ 中，用赋值而非构造函数初始化 Qt 对象更安全，或使用大括号初始化 `QNetworkRequest{QUrl(url)}`。

---

## 七、测试与验证类

### 7.1 测试文件部署混乱

**现象**：测试文件散落在 WebAISearch 和 Certus 两个目录中，运行位置不明确。

**根因**：项目从 WebAISearch 演进而来，测试文件未跟随迁移。

**解决**：将全部 9 个测试文件集中到 `agent/tests/`，修复跨项目引用。

**教训**：项目迁移时必须同时迁移测试基础设施，并在新位置验证全部测试通过。

### 7.2 测试依赖配置文件的隐藏假设

**现象**：`test_gemini` 失败，`detect_platform()` 返回 "unknown"。

**根因**：`config.json` 的 `platform_urls` 缺少 gemini 条目，而 platform 检测依赖此配置。

**解决**：在 `config.json` 中添加 gemini URL。

**教训**：测试应该要么 mock 外部依赖，要么在 CI 中显式检查预条件。

---

## 八、代码质量类

### 8.1 API Key 硬编码绕过

**现象**：`generator.py` 和 `prompt_builder.py` 使用 `api_key="local"` 调用 OpenAI SDK，完全绕过了真实 API Key 配置。

**根因**：开发阶段使用的占位符未被清理。

**解决**：全部替换为统一的 `call_deepseek_api()` 调用。

**教训**：占位符或硬编码值必须在合入前清理。代码审查应特别关注认证相关参数。

### 8.2 重复的 API 调用实现

**现象**：6 处代码各自实现 DeepSeek API 调用，参数配置不一致。

**根因**：没有统一的 API 调用封装。

**解决**：创建 `call_deepseek_api()` 统一入口，所有调用方改为引用此函数。

**教训**：出现第三次重复时就应该抽取公共函数。重复代码不仅维护困难，还会导致行为不一致。

---

## 九、关键经验

1. **同类模块出现 2 个以上 → 立刻抽取公共逻辑**。平台脚本的教训最深刻：修了一个 Bug，其他 3 个还有同样问题。
2. **进程间通信的每一处失败都要显式传播**。Silent failure 是排查最耗时的问题。
3. **自动化修改代码必须谨慎**。LLM 生成的修复在没有测试保护的情况下直接应用，弊大于利。
4. **状态管理要单一数据源**。多个 UI 组件显示同一信息时，必须共享同一个数据源或建立交叉校验。
5. **超时值要基于实际测量**。10s 的 CDP 等待对冷启动浏览器不够，但对握手来说又太长。
6. **测试是迁移的一部分**。从旧项目搬代码时，必须同时搬测试并验证通过。
7. **AI 对话平台的 DOM 特殊**。contenteditable 在页面中可能出现多次（AI 回复也可编辑），不能简单取第一个。
