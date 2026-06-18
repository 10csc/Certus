# -*- coding: utf-8 -*-
"""进化 Agent —— 独立进程，不依赖 Playwright/浏览器。

五阶段 LLM 驱动自修复流程：
  1. 提取 (extract)    —— 收集崩溃现场：stderr traceback + failure_log
  2. 读代码 (read_code) —— 读取相关源文件，理解当前实现
  3. 分析 (analyze)    —— LLM 对比代码+错误，定位根因
  4. 重写 (rewrite)    —— LLM 生成修复方案，应用到代码/配置
  5. 测试 (test)       —— mock 模式验证修复效果

与搜索 Agent 完全隔离：不 import Playwright，不连接 CDP，不操作浏览器。
每阶段推 stage_start / stage_done 事件，C++ 端分阶段计时。
"""

import sys
import os
import time
import json
import subprocess
import threading

# 协议层复用（与搜索 Agent 相同的通信协议）
from agent_protocol import (
    write_frame, handshake, SUPPORTED_PROTOCOL,
    pipe_broken, cancel_flag, ping_received,
    emit_evolution_start, emit_evolution_done, emit_evolution_failed,
    stdin_thread_died,
)

# 进化引擎复用
from evolution import (
    FailureAnalyzer, StrategyAdapter, load_or_create_profile,
    GlobalKnowledge, KnowledgePropagator,
)
from common import call_deepseek_api

# === 阶段超时（秒） ===
STAGE_TIMEOUTS = {
    "extract":   30,
    "read_code": 30,
    "analyze":   120,
    "rewrite":   120,
    "test":      120,
}
GLOBAL_TIMEOUT = 420


class EvolutionError(Exception):
    """进化阶段错误，携带阶段名和原因。"""
    def __init__(self, stage, reason):
        self.stage = stage
        self.reason = reason
        super().__init__(f"[{stage}] {reason}")


def send_stage_start(name, current, total, description):
    """推送进化阶段开始事件。"""
    write_frame("stage_start",
                stage=f"evolution_{name}",
                question=f"阶段 {current}/{total}: {description}",
                platform="evolution")
    print(f"[Evolution] 阶段 {current}/{total}: {description}", file=sys.stderr)


def send_stage_done(name, result):
    """推送进化阶段完成事件。"""
    payload = {
        "stage": f"evolution_{name}",
        "platform": "evolution",
        "content_len": len(json.dumps(result, ensure_ascii=False)),
        "evolution_stage": name,
    }
    payload.update(result)
    write_frame("stage_done", **payload)
    print(f"[Evolution]   ✓ {name} 完成", file=sys.stderr)


# ============================================================
# 阶段1: 提取
# ============================================================

def stage_extract(diagnosis, agent_dir):
    """收集崩溃现场：stderr traceback + failure_records。

    返回: {error_type, platform, detail, content, failure_records, data_dir}
    """
    error_type = diagnosis.get("error_type", "crash")
    platform = diagnosis.get("platform", "system")
    detail = diagnosis.get("detail", "")

    # 读取 stderr 日志（如果有）
    stderr_tail = ""
    stderr_path = diagnosis.get("stderr_path", "")
    if not stderr_path:
        # 尝试默认路径
        candidates = [
            os.path.join(agent_dir, "..", "data", "agent_stderr.log"),
            os.path.join(os.getcwd(), "data", "agent_stderr.log"),
        ]
        for p in candidates:
            if os.path.exists(p):
                stderr_path = p
                break

    if stderr_path and os.path.exists(stderr_path):
        try:
            with open(stderr_path, "r", encoding="utf-8", errors="replace") as f:
                lines = f.readlines()
                stderr_tail = "".join(lines[-50:])  # 最后 50 行
        except Exception:
            pass

    result = {
        "error_type": error_type,
        "platform": platform,
        "detail": detail,
        "stderr_tail": stderr_tail[-3000:],  # 限制 3K
        "data_dir": os.path.join(agent_dir, "..", "data"),
    }
    return result


# ============================================================
# 阶段2: 读代码
# ============================================================

# error_type → 相关源文件映射
ERROR_TYPE_FILES = {
    "extract_failed":  ["extractor.py", "common.py"],
    "send_failed":     ["common.py"],
    "cdp_failed":      ["common.py", "agent.py"],
    "timeout":         ["orchestrator.py", "common.py", "agent_protocol.py"],
    "crash":           ["agent.py", "common.py", "agent_protocol.py"],
    "handshake_timeout": ["agent_protocol.py", "agent.py"],
    "heartbeat_timeout": ["agent.py", "agent_protocol.py"],
}

# 默认兜底
FALLBACK_FILES = ["agent.py", "common.py", "agent_protocol.py", "orchestrator.py"]


def stage_read_code(crash_info, agent_dir):
    """根据 error_type 确定相关源文件并读取。

    返回: {filepath: content, ...}，总内容限制 50K 字符。
    """
    error_type = crash_info.get("error_type", "crash")
    platform = crash_info.get("platform", "")

    file_names = ERROR_TYPE_FILES.get(error_type, FALLBACK_FILES)

    # 如果有明确的平台名，也读对应平台脚本
    if platform and platform != "system":
        platform_file = f"platforms/{platform}.py"
        if platform_file not in file_names:
            file_names = list(file_names) + [platform_file]

    code_files = {}
    total_chars = 0
    max_total = 50000

    for fname in file_names:
        fpath = os.path.join(agent_dir, fname)
        parts = fname.replace("\\", "/").split("/")
        if len(parts) > 1 and parts[0] == "platforms":
            fpath = os.path.join(agent_dir, "platforms", parts[1])

        if not os.path.exists(fpath):
            continue

        try:
            with open(fpath, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
        except Exception:
            continue

        if total_chars + len(content) > max_total:
            # 截断此文件
            remaining = max_total - total_chars
            content = content[:remaining] + "\n# ... (内容截断)"
        code_files[fname] = content
        total_chars += len(content)

        if total_chars >= max_total:
            break

    return code_files


# ============================================================
# 阶段3: 分析
# ============================================================

ANALYSIS_SYSTEM_PROMPT = """你是资深 Python 调试专家。你的任务是分析代码崩溃的根因。

请按以下格式回复：
```
ROOT_CAUSE: <一句话描述根因>
FILE: <出问题的文件名>
LINE: <大致行号或函数名>
SEVERITY: high|medium|low
ANALYSIS: <详细分析，包括为什么这段代码会触发错误>
POSSIBLE_FIXES: <2-3 个可能的修复方向>
```
只分析，不写具体修复代码。修复代码由下一阶段的 Agent 负责。"""


def stage_analyze(crash_info, code_files):
    """LLM 分析崩溃根因。

    返回: {root_cause, file, severity, analysis, possible_fixes}
    失败返回 None。
    """
    error_type = crash_info.get("error_type", "crash")
    platform = crash_info.get("platform", "system")
    detail = crash_info.get("detail", "")
    stderr_tail = crash_info.get("stderr_tail", "")

    # 构建 user prompt
    parts = [
        f"## 崩溃信息",
        f"错误类型: {error_type}",
        f"发生平台: {platform}",
        f"错误详情: {detail}",
    ]
    if stderr_tail.strip():
        parts.append(f"\n## stderr 最后输出\n```\n{stderr_tail.strip()}\n```")

    parts.append("\n## 相关源代码")
    for fname, content in code_files.items():
        parts.append(f"\n### {fname}\n```python\n{content[:5000]}\n```")

    user_prompt = "\n".join(parts)

    print(f"[Evolution] 分析: error={error_type}, platform={platform}, "
          f"code_files={list(code_files.keys())}", file=sys.stderr)

    answer = call_deepseek_api(
        system_prompt=ANALYSIS_SYSTEM_PROMPT,
        user_prompt=user_prompt,
        model="deepseek-v4-pro",
        max_tokens=4096,
        temperature=0.2,
        timeout=STAGE_TIMEOUTS["analyze"],
    )

    if not answer:
        print("[Evolution] LLM 分析返回空", file=sys.stderr)
        return None

    # 解析 LLM 输出
    import re
    result = {}
    for key in ("ROOT_CAUSE", "FILE", "LINE", "SEVERITY", "ANALYSIS", "POSSIBLE_FIXES"):
        m = re.search(rf"{key}:\s*(.+?)(?=\n[A-Z_]+:|\Z)", answer, re.DOTALL)
        if m:
            result[key.lower()] = m.group(1).strip()

    if not result.get("root_cause"):
        result["root_cause"] = "LLM 返回格式不符合预期，请检查原始输出"
        result["raw_output"] = answer[:1000]
        print(f"[Evolution] 分析结果解析失败: {answer[:200]}", file=sys.stderr)
        return result  # 即使解析失败也返回，让重写阶段尝试处理

    print(f"[Evolution] 根因: {result.get('root_cause', '?')[:100]}", file=sys.stderr)
    return result


# ============================================================
# 阶段4: 重写
# ============================================================

REWRITE_SYSTEM_PROMPT = """你是 Python 代码修复专家。基于上一阶段的根因分析，生成具体的代码修复。

**规则**:
1. 只输出需要修改的部分，不要输出完整文件
2. 用以下格式描述每个修改：
   ```
   FILE: <文件名>
   FIND: <要替换的代码片段（精确匹配，含缩进）>
   REPLACE: <替换后的代码片段>
   REASON: <修改理由>
   ---
   ```
3. 保持最小改动原则——只修问题，不做无关重构
4. 如果修复涉及配置文件（非 .py），使用 JSON_PATH 代替 FIND/REPLACE
5. 改动必须可验证——不要写"可能"、"也许"这样的词"""


def stage_rewrite(analysis, code_files):
    """LLM 生成修复代码。

    返回: [{file, operation, old, new, reason}, ...]
    失败返回 None。
    """
    root_cause = analysis.get("root_cause", "")
    target_file = analysis.get("file", "")
    severity = analysis.get("severity", "medium")
    detailed_analysis = analysis.get("analysis", "")

    parts = [
        f"## 根因分析",
        f"问题: {root_cause}",
        f"出问题文件: {target_file}",
        f"严重性: {severity}",
        f"详细分析: {detailed_analysis}",
    ]

    # 附上目标文件完整内容
    if target_file and target_file in code_files:
        parts.append(f"\n## {target_file} (完整内容)\n```python\n{code_files[target_file]}\n```")

    # 也附上相关文件
    for fname, content in code_files.items():
        if fname != target_file:
            parts.append(f"\n## {fname}\n```python\n{content[:3000]}\n```")

    user_prompt = "\n".join(parts)

    answer = call_deepseek_api(
        system_prompt=REWRITE_SYSTEM_PROMPT,
        user_prompt=user_prompt,
        model="deepseek-v4-pro",
        max_tokens=8192,
        temperature=0.1,
        timeout=STAGE_TIMEOUTS["rewrite"],
    )

    if not answer:
        print("[Evolution] LLM 重写返回空", file=sys.stderr)
        return None

    # 解析改动脉络
    import re
    changes = []
    blocks = answer.split("\n---")
    for block in blocks:
        block = block.strip()
        if not block:
            continue
        change = {}
        for key in ("FILE", "FIND", "REPLACE", "REASON"):
            m = re.search(rf"{key}:\s*\n?(.+?)(?=\n(?:FILE|FIND|REPLACE|REASON):|\Z)", block, re.DOTALL)
            if m:
                change[key.lower()] = m.group(1).strip()
        if change.get("file"):
            changes.append(change)

    if not changes:
        # 可能 LLM 用了不同的格式，尝试解析 JSON_PATH 风格
        for key in ("JSON_PATH", "JSON_VALUE"):
            m = re.search(rf"{key}:\s*(.+?)(?:\n|$)", answer)
            if m:
                changes.append({
                    "file": "config",
                    "json_path": m.group(1).strip() if key == "JSON_PATH" else "",
                    "reason": "从 LLM 输出解析",
                })

    if not changes:
        print(f"[Evolution] 重写结果解析失败: {answer[:200]}", file=sys.stderr)
        # 兜底：保留 LLM 原始输出
        changes = [{"file": "analysis_only", "raw": answer[:3000], "reason": "LLM 输出格式异常"}]

    # 应用改动到文件
    agent_dir = os.path.dirname(os.path.abspath(__file__))
    applied = 0
    for ch in changes:
        fname = ch.get("file", "")
        if fname in ("config", "analysis_only"):
            continue  # 配置变更由 StrategyAdapter 处理

        fpath = os.path.join(agent_dir, fname)
        if not os.path.exists(fpath):
            ch["status"] = "file_not_found"
            continue

        try:
            with open(fpath, "r", encoding="utf-8") as f:
                original = f.read()

            find_text = ch.get("find", "")
            replace_text = ch.get("replace", "")

            if find_text and find_text in original:
                modified = original.replace(find_text, replace_text, 1)
                with open(fpath, "w", encoding="utf-8") as f:
                    f.write(modified)
                ch["status"] = "applied"
                applied += 1
            elif find_text:
                ch["status"] = "find_not_matched"
            else:
                ch["status"] = "no_find_pattern"
        except Exception as e:
            ch["status"] = f"error: {e}"

    print(f"[Evolution] 重写: {len(changes)} 处改动, {applied} 已应用", file=sys.stderr)
    return changes


# ============================================================
# 阶段5: 测试
# ============================================================

def stage_test(changes, agent_dir):
    """Mock 模式验证修复效果。

    启动 agent.py --mock-cdp 验证修复后不再报错。
    返回: {passed: bool, output_summary: str}
    """
    # 确定测试用的 mock 文件
    mock_file = os.path.join(agent_dir, "tests", "mock_data", "search_deepseek_ok.json")
    test_query = "测试修复效果"

    cmd = [
        sys.executable,
        os.path.join(agent_dir, "agent.py"),
        "--mock-cdp",
        f"--mock-file={mock_file}",
        "--depth=L2",
        test_query,
    ]

    print(f"[Evolution] 测试: {' '.join(cmd)}", file=sys.stderr)

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=STAGE_TIMEOUTS["test"],
            cwd=agent_dir,
            env={**os.environ, "PYTHONUTF8": "1"},
        )

        # 分析输出
        stdout_lines = result.stdout.strip().split("\n") if result.stdout.strip() else []
        stderr_lines = result.stderr.strip().split("\n") if result.stderr.strip() else []

        # 检查是否有 done 事件
        has_done = any('"done"' in line or '"event":"done"' in line for line in stdout_lines)
        # 检查是否有 error 事件
        has_error = any('"error"' in line for line in stdout_lines)
        # 进程退出码
        ok_exit = result.returncode == 0

        passed = has_done and not has_error and ok_exit

        summary = {
            "exit_code": result.returncode,
            "has_done": has_done,
            "has_error": has_error,
            "stdout_lines": len(stdout_lines),
            "stderr_tail": "".join(stderr_lines[-10:]) if stderr_lines else "",
        }

        print(f"[Evolution] 测试结果: passed={passed}, exit={result.returncode}, "
              f"done={has_done}, error={has_error}", file=sys.stderr)

        return {"passed": passed, "output_summary": json.dumps(summary, ensure_ascii=False)}

    except subprocess.TimeoutExpired:
        print("[Evolution] 测试超时", file=sys.stderr)
        return {"passed": False, "output_summary": "测试超时"}
    except Exception as e:
        print(f"[Evolution] 测试异常: {e}", file=sys.stderr)
        return {"passed": False, "output_summary": str(e)}


# ============================================================
# 主入口
# ============================================================

def _stdin_listener():
    """stdin 监听线程：接收 cancel/ping。与搜索 Agent 相同逻辑。"""
    print("[Thread] evolution stdin 监听线程启动", file=sys.stderr)
    try:
        from agent_protocol import read_frame as read_prefix_frame
        for frame in read_prefix_frame(sys.stdin.buffer):
            if pipe_broken.is_set():
                break
            action = frame.get("action", "")
            if action == "cancel":
                cancel_flag.set()
                print("[Ctrl] 收到取消指令", file=sys.stderr)
            elif action == "ping":
                ping_received.set()
            else:
                print(f"[Ctrl] 未知指令: {action}", file=sys.stderr)
    except Exception as e:
        print(f"[Thread] evolution stdin 监听线程退出: {e}", file=sys.stderr)
    finally:
        stdin_thread_died.set()


def _heartbeat():
    """heartbeat 线程：独立回 pong。"""
    print("[Thread] evolution heartbeat 线程启动", file=sys.stderr)
    while not cancel_flag.is_set():
        if pipe_broken.is_set():
            print("[Heartbeat] 管道破裂 → 退出", file=sys.stderr)
            break
        if stdin_thread_died.is_set():
            print("[Heartbeat] stdin 监听已死 → 退出", file=sys.stderr)
            break
        if ping_received.wait(timeout=1.0):
            ping_received.clear()
            write_frame("pong")
            if pipe_broken.is_set():
                break


def run_evolution(config):
    """进化主入口，五阶段串行执行。

    不依赖 Playwright，不连接 CDP。
    每阶段独立超时 + 全局 420s 兜底。
    """
    diagnosis = config.get("diagnosis", {})
    agent_dir = config.get("agent_dir",
                           os.path.dirname(os.path.abspath(__file__)))

    if not diagnosis:
        emit_evolution_failed("缺少诊断数据")
        return

    start_time = time.time()

    try:
        # === 阶段1: 提取 ===
        send_stage_start("extract", 1, 5, "收集崩溃现场...")
        crash_info = stage_extract(diagnosis, agent_dir)
        send_stage_done("extract", {
            "error_type": crash_info["error_type"],
            "platform": crash_info["platform"],
            "has_stderr": bool(crash_info.get("stderr_tail")),
        })

        # === 阶段2: 读代码 ===
        send_stage_start("read_code", 2, 5, "读取相关源代码...")
        code_files = stage_read_code(crash_info, agent_dir)
        if not code_files:
            raise EvolutionError("read_code", "未找到相关源文件")
        send_stage_done("read_code", {
            "files": list(code_files.keys()),
            "total_chars": sum(len(v) for v in code_files.values()),
        })

        # === 阶段3: 分析 ===
        send_stage_start("analyze", 3, 5, "LLM 分析根因...")
        analysis = stage_analyze(crash_info, code_files)
        if not analysis or not analysis.get("root_cause"):
            raise EvolutionError("analyze", "LLM 无法确定根因")
        send_stage_done("analyze", {
            "root_cause": analysis.get("root_cause", "")[:200],
            "file": analysis.get("file", ""),
            "severity": analysis.get("severity", "medium"),
        })

        # 检查全局超时
        if time.time() - start_time > GLOBAL_TIMEOUT:
            raise EvolutionError("analyze", "全局超时（已完成3/5阶段）")

        # === 阶段4: 重写 ===
        send_stage_start("rewrite", 4, 5, "LLM 生成修复方案...")
        changes = stage_rewrite(analysis, code_files)
        if not changes:
            raise EvolutionError("rewrite", "LLM 未能生成有效修复")
        applied_count = sum(1 for c in changes if c.get("status") == "applied")
        send_stage_done("rewrite", {
            "change_count": len(changes),
            "applied": applied_count,
        })

        if time.time() - start_time > GLOBAL_TIMEOUT:
            raise EvolutionError("rewrite", "全局超时（已完成4/5阶段）")

        # === 阶段5: 测试 ===
        send_stage_start("test", 5, 5, "Mock 验证修复效果...")
        test_result = stage_test(changes, agent_dir)
        send_stage_done("test", test_result)

        # 全部通过
        change_list = [
            f"{c.get('file', '?')}: {c.get('reason', '?')[:80]}"
            for c in changes if c.get("status") == "applied"
        ]
        emit_evolution_done(change_list, fixed=test_result.get("passed", False))

    except EvolutionError as e:
        elapsed = time.time() - start_time
        print(f"[Evolution] 失败: [{e.stage}] {e.reason} (耗时 {elapsed:.0f}s)", file=sys.stderr)
        emit_evolution_failed(f"阶段 {e.stage} 失败: {e.reason}")
    except Exception as e:
        elapsed = time.time() - start_time
        print(f"[Evolution] 异常: {e} (耗时 {elapsed:.0f}s)", file=sys.stderr)
        import traceback
        traceback.print_exc(file=sys.stderr)
        emit_evolution_failed(f"未预期的异常: {str(e)[:500]}")
    finally:
        sys.stdout.flush()


def run_protocol_mode():
    """Protocol 模式入口：等待 C++ hello → 启动线程 → 执行五阶段进化。"""
    config, mode = handshake(SUPPORTED_PROTOCOL)

    if mode != "evolution":
        print(f"[Evolution] 警告: mode={mode}，期望 evolution", file=sys.stderr)

    # 启动 stdin 监听线程
    stdin_thread = threading.Thread(target=_stdin_listener, daemon=True)
    stdin_thread.start()

    # 启动 heartbeat 线程
    heartbeat_thread = threading.Thread(target=_heartbeat, daemon=True)
    heartbeat_thread.start()

    # 主线程：执行五阶段进化
    run_evolution(config)

    # 等线程自然退出
    sys.exit(0)


# ============================================================
# CLI 入口（向后兼容直接调用）
# ============================================================

if __name__ == "__main__":
    run_protocol_mode()
