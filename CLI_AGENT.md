# Certus CLI —— 外部 Agent 搜索接入指南

供 AI Agent（Claude Code、Codex、Cursor 等）通过命令行调用 Certus 后端执行联网深度搜索。

## 命令概览

| 命令 | 用途 | 格式 |
|------|------|------|
| `search` | 执行搜索 | `certus_backend search "问题" [选项]` |
| `status` | 查看状态 | `certus_backend status [--json]` |
| `validate` | 配置验证 | `certus_backend validate [--json]` |
| `browser` | 浏览器管理 | `certus_backend browser --start/--stop` |
| `config` | 查看/修改配置 | `certus_backend config --cdp-port PORT` |

## 搜索命令

```bash
# 基础搜索
certus_backend search "查询问题"

# Agent 模式（JSON 输出 + stdin 管道）
echo "查询问题" | certus_backend search --stdin --json

# 指定输出文件
certus_backend search "查询问题" -o /path/result.md

# L3 深度（交叉验证）
certus_backend search "查询问题" --depth L3
```

### 搜索选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `--depth L2/L3` | 搜索深度 | L2 |
| `--json` | JSON 行输出（Agent 解析用） | 无（人类可读） |
| `--stdin` | 从标准输入读取查询 | 无 |
| `--search-platform` | 搜索平台 | deepseek |
| `--synthesis-platform` | 整合平台 | deepseek |
| `--cdp-port` | 浏览器 CDP 端口 | 9223 |
| `-o, --output` | 报告输出路径 | 无（仅写入 data/latest_result.md） |

### JSON 事件协议

Agent 必须解析以下 JSON 行事件：

```json
// 搜索开始
{"event":"search_started","query":"...","depth":"L2","search_platform":"deepseek","synthesis_platform":"deepseek"}

// 阶段开始（search_1/search_2/synthesis/review）
{"event":"stage_start","stage":"search_1","question":"...","platform":"deepseek"}

// 进度更新（elapsed_sec: 秒, content_len: 字符数）
{"event":"stage_progress","stage":"search_1","platform":"deepseek","elapsed_sec":10,"content_len":1500}

// 阶段完成
{"event":"stage_done","stage":"search_1","platform":"deepseek","content_len":3495}

// 搜索完成（读取 report_path 获取报告）
{"event":"search_finished","success":true,"report_path":"F:/CodeFile/Certus/data/latest_result.md"}

// 搜索取消/失败
{"event":"search_cancelled","partial_result_len":"..."}
{"event":"error","error_type":"search_failed","detail":"..."}
```

## Agent 集成流程

```
1. 验证环境
   certus_backend validate --json

2. 确保浏览器就绪
   certus_backend browser --start --port 9223

3. 执行搜索（监听 JSON 行，等待 search_finished）
   certus_backend search "查询问题" --json

4. 收到 search_finished 后读报告
   cat data/latest_result.md
```

## Agent 示例（Bash 伪代码）

```bash
# 搜索并在完成时立即读取报告
certus_backend search "$QUERY" --json | while read -r line; do
  echo "$line"
  if echo "$line" | grep -q '"search_finished"'; then
    cat data/latest_result.md
    break
  fi
done
```

## 报告格式

报告是标准 Markdown，结构：

```
# 研究报告: <查询主题>
## 1. <平台名> — <问题>
> 内容长度: N 字符 | 引用链接: N 个
[搜索内容...]
### 来源评价
- 平均可信度: X/10
## 交叉验证报告
- 验证级别: single_source/multi_source
```
