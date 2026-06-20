# -*- coding: utf-8 -*-
"""Certus Python Agent 入口 —— 三线程模型 + stdin/stdout JSON 协议。

架构：
  主线程（搜索）      ← orchestrator.execute(on_event=on_event)
  stdin 监听线程      ← sys.stdin.readline() 阻塞读，接收 cancel/ping
  heartbeat 线程      ← 独立回 pong，5s 内响应，搜索卡死不影响心跳

模式：
  - Protocol 模式（默认）：handshake → 接收 config → 运行搜索 → 退出
  - CLI 模式：python agent.py "query" --depth L2  (向后兼容)
  - Mock CDP 模式：python agent.py --mock-cdp "query"  (无浏览器测试)

所有日志走 stderr，stdout 只走 JSON 事件流。
"""

import sys
import os
import time
import json
import argparse
import threading

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from agent_protocol import (
    on_event, write_frame,
    cancel_flag, ping_received,
    emit_stage_start, emit_stage_done, emit_done,
    emit_error, emit_cancelled,
    handshake, SUPPORTED_PROTOCOL,
)
from common import set_runtime_config, load_config
from runtime_paths import DATA_DIR


# ============================================================
# 主搜索流程
# ============================================================


def agent_search(user_query, depth="L2", project_context=None,
                 force_platform=None, quick=False, output_dir=None):
    """Agent 主流程（保留原有逻辑，增加 on_event 回调传递）。

    返回:
        {
            "query": str, "tool_choice": {...}, "plan": {...},
            "execution": {...}, "report": str, "report_path": str,
            "validation": {...}, "workspace_summary": {...}, "elapsed_sec": int,
        }
    """
    from tool_router import route as route_tool
    from planner import plan as make_plan, format_plan
    from orchestrator import execute as exec_plan, execute_simple
    from synthesizer import synthesize, generate_report, cross_validate
    from workspace import WorkspaceState, record_episode
    from diagnostics import diagnose, format_diagnosis

    start_time = time.time()
    config = load_config()

    # Step 0: 工具选择
    tool_choice = route_tool(user_query)
    actual_tool = tool_choice["tool"]
    actual_depth = tool_choice["depth"]

    # 用户显式指定深度时，不被 ToolRouter 覆盖
    user_specified_depth = depth and depth in ("L1", "L2", "L3")
    if user_specified_depth:
        actual_depth = depth
    elif actual_tool == "WebSearch":
        actual_depth = "L1"

    print(f"[Agent] 工具: {actual_tool} | 深度: {actual_depth} | 理由: {tool_choice['reason']}",
          file=sys.stderr)

    from orchestrator import check_platform_config
    print(f"[Agent] 平台配置:\n{check_platform_config()}", file=sys.stderr)

    if actual_depth == "L1":
        return {
            "query": user_query, "tool_choice": tool_choice,
            "plan": None, "execution": None,
            "report": f"[WebSearch] {user_query}",
            "note": "L1 查询应由调用方 Agent 使用 WebSearch 工具处理",
        }

    # Step 1: 规划
    plan_dict = make_plan(user_query, actual_depth, project_context)
    if force_platform:
        for sq in plan_dict["sub_questions"]:
            sq["platform"] = force_platform
            sq["reason"] = "用户指定平台"

    print(format_plan(plan_dict), file=sys.stderr)

    workspace = WorkspaceState()
    workspace.set_query(user_query)
    workspace.set_plan(plan_dict)

    # Step 2: 执行（传入 on_event 回调）
    try:
        if quick:
            platform = force_platform or plan_dict["sub_questions"][0]["platform"]
            content = execute_simple(user_query, platform, actual_depth,
                                     config=config)
            results = [{"question": user_query, "platform": platform,
                        "content": content, "gaps": [], "links": [],
                        "content_len": len(content) if content else 0}]
            execution = {"results": results, "gaps_total": 0, "all_links": []}
        else:
            execution = exec_plan(plan_dict, on_event=on_event,
                                  config=config)
            for r in execution.get("results", []):
                workspace.add_result(
                    r.get("platform", "?"),
                    r.get("question", "")[:100],
                    r.get("content_len", 0),
                    len(r.get("gaps", [])),
                    len(r.get("links", [])),
                )
    except Exception as e:
        from orchestrator import ConfigError
        error_type = "config_error" if isinstance(e, ConfigError) else "execution_failed"
        print(f"\n[Agent] 执行失败 ({error_type}): {e}", file=sys.stderr)
        emit_error(error_type, platform="system", detail=str(e))
        if error_type == "config_error":
            raise  # 配置错误直接终止，不做诊断
        search_platform = (
            plan_dict["sub_questions"][0].get("platform", "deepseek")
            if plan_dict.get("sub_questions") else "deepseek"
        )
        diag = diagnose("send_failed", context={"platform": search_platform})
        print(format_diagnosis(diag), file=sys.stderr)
        raise

    # Step 3: 内容有效性检查 —— 没有任何有效内容 = 搜索失败
    valid_results = [r for r in execution.get("results", []) if r.get("content")]
    if not valid_results:
        fail_reason = "所有平台均未返回有效内容"
        print(f"\n[Agent] ✗ 搜索失败: {fail_reason}", file=sys.stderr)
        emit_error("search_failed", platform="system",
                   detail=f"{fail_reason}。请检查：1) 平台链接是否为聊天页 URL  2) 定型是否通过实际收发验证")
        return {
            "query": user_query,
            "error": fail_reason,
            "results": execution.get("results", []),
        }

    # Step 4: 最终审阅（DeepSeek API 审查整理，确保输出质量）
    from orchestrator import final_review
    sp = config.get("search_platform", "deepseek")
    kp = config.get("synthesis_platform", "deepseek")
    on_event("stage_start", stage="review", question="最终审阅整理...", platform="本地API")
    reviewed, review_meta = final_review(valid_results, user_query)
    if reviewed:
        report = reviewed
        validation = {"final_review": review_meta, "source_count": len(valid_results)}
        on_event("stage_done", stage="review", platform="本地API",
                 content_len=len(report))
        print(f"[Agent] 最终审阅完成 | 报告 {len(report)} 字符", file=sys.stderr)
    else:
        # API 审阅失败（如无 key），用本地合成兜底
        on_event("stage_start", stage="synthesis", question="本地整合中...", platform=kp)
        report, validation = synthesize(user_query, execution)
        on_event("stage_done", stage="review", platform="本地API",
                 content_len=len(report), status="local_fallback",
                 error=review_meta.get("reason", "API 不可用"))
        print(f"[Agent] 本地合成完成（API审阅跳过: {review_meta.get('reason', '未知')}）", file=sys.stderr)

    # 输出报告
    data_dir = output_dir or DATA_DIR
    os.makedirs(data_dir, exist_ok=True)
    report_path = os.path.join(data_dir, "latest_result.md")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write(report)
    print(f"\n[Agent] 报告: {report_path} ({len(report)} 字符)", file=sys.stderr)

    # 记录记忆
    elapsed = int(time.time() - start_time)
    for r in execution.get("results", []):
        if r.get("content"):
            record_episode(
                "certus", r["platform"], r.get("question", "")[:100],
                actual_depth, elapsed,
                credibility=validation.get("confidence", "中") == "高" and 8 or 6,
                content_len=r.get("content_len", 0),
                gaps_count=len(r.get("gaps", [])),
            )

    workspace.complete()
    summary = workspace.get_summary()

    print(f"\n[Agent] 完成 | 耗时: {elapsed}s | 结果: {summary['results']}个 | "
          f"置信度: {validation.get('confidence', 'N/A')}", file=sys.stderr)

    return {
        "query": user_query, "tool_choice": tool_choice,
        "plan": plan_dict, "execution": execution,
        "report": report, "report_path": report_path,
        "validation": validation, "workspace_summary": summary,
        "elapsed_sec": elapsed,
    }


# ============================================================
# Mock CDP 搜索（无浏览器测试）
# ============================================================


def run_mock_search(query, mock_file=None, depth="L2"):
    """Mock CDP 模式：加载预制 JSON 替代平台回复，验证事件流和状态机。

    跳过 playwright 和浏览器，直接推事件到 stdout。
    """
    mock_dir = os.path.join(SCRIPT_DIR, "tests", "mock_data")

    if mock_file:
        mock_path = mock_file if os.path.isfile(mock_file) else os.path.join(mock_dir, mock_file)
    else:
        mock_path = os.path.join(mock_dir, "search_deepseek_ok.json")

    print(f"[Mock] 加载: {mock_path}", file=sys.stderr)

    try:
        with open(mock_path, "r", encoding="utf-8") as f:
            mock = json.load(f)
    except FileNotFoundError:
        emit_error("mock_file_missing", platform="system",
                   detail=f"Mock 文件不存在: {mock_path}")
        sys.exit(1)
    except json.JSONDecodeError as e:
        emit_error("mock_json_error", platform="system", detail=str(e))
        sys.exit(1)

    mock_type = mock.get("_type", "search_ok")

    # 根据 mock 类型模拟搜索生命周期
    if mock_type == "search_ok":
        return _mock_search_ok(query, mock, depth)
    elif mock_type == "search_timeout":
        return _mock_search_timeout(query, mock, depth)
    elif mock_type == "platform_reject":
        return _mock_platform_reject(query, mock, depth)
    elif mock_type == "extract_fail":
        return _mock_extract_fail(query, mock, depth)
    elif mock_type == "synthesis_ok":
        return _mock_synthesis_ok(query, mock, depth)
    elif mock_type == "synthesis_empty":
        return _mock_synthesis_empty(query, mock, depth)
    else:
        emit_error("unknown_mock_type", platform="system",
                   detail=f"未知 mock 类型: {mock_type}")
        sys.exit(1)


def _mock_search_ok(query, mock, depth):
    """模拟正常搜索流程（含 cancel 检查 + 足够延迟让 heartbeat 响应 ping）。"""
    platform = mock.get("platform", "deepseek")
    content = mock.get("content", "这是模拟的搜索结果内容。")
    content_len = len(content)
    reliability = mock.get("reliability", {"confirmed": 8, "inferred": 2, "unconfirmed": 0, "reliable": True})

    # 模拟搜索阶段（每 0.1s 检查一次 cancel，总时长确保心跳可响应）
    emit_stage_start("search_1", query, platform)
    for _ in range(15):  # 1.5s 模拟延迟
        if cancel_flag.is_set():
            emit_cancelled("搜索阶段被取消")
            return {"status": "cancelled"}
        time.sleep(0.1)
    emit_stage_done("search_1", platform, content_len, reliability=reliability)

    # 写入报告
    os.makedirs(DATA_DIR, exist_ok=True)
    report_path = os.path.join(DATA_DIR, "_mock_result.md")
    report = mock.get("report", f"# Mock 搜索报告\n\n**问题**: {query}\n\n{content}")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write(report)

    # 整合阶段延迟
    for _ in range(5):
        if cancel_flag.is_set():
            emit_cancelled("整合阶段被取消")
            return {"status": "cancelled"}
        time.sleep(0.1)

    emit_done(elapsed_sec=2.0, report_path=report_path, content_len=content_len)
    return {"status": "done", "report_path": report_path, "content_len": content_len}


def _mock_search_timeout(query, mock, depth):
    """模拟搜索超时。"""
    platform = mock.get("platform", "deepseek")

    emit_stage_start("search_1", query, platform)
    time.sleep(0.1)
    emit_stage_done("search_1", platform, 0, status="timeout", error="提取超时")

    emit_error("timeout", platform=platform, detail="模拟超时: AI 回复超过 180s")
    return {"status": "timeout"}


def _mock_platform_reject(query, mock, depth):
    """模拟平台拒绝（高峰期）。"""
    platform = mock.get("platform", "deepseek")
    reject_msg = mock.get("reject_message", "高峰期算力不足，请稍后重试")

    emit_stage_start("search_1", query, platform)
    time.sleep(0.1)
    emit_stage_done("search_1", platform, 0, status="rejected",
                    error=reject_msg, suggestion="try_other_platform")

    emit_error("platform_rejected", platform=platform, detail=reject_msg)
    return {"status": "rejected"}


def _mock_extract_fail(query, mock, depth):
    """模拟提取失败。"""
    platform = mock.get("platform", "deepseek")

    emit_stage_start("search_1", query, platform)
    time.sleep(0.1)
    emit_stage_done("search_1", platform, 0, status="extract_failed",
                    error="DOM 提取为空，textContent 兜底也失败")

    emit_error("extract_failed", platform=platform,
               detail=mock.get("detail", "选择器未匹配任何元素"))
    return {"status": "extract_failed"}


def _mock_synthesis_ok(query, mock, depth):
    """模拟正常整合。"""
    platform = mock.get("platform", "kimi")
    content = mock.get("content", "这是模拟的整合结果。")
    content_len = len(content)

    emit_stage_start("search_1", query, "deepseek")
    time.sleep(0.05)
    emit_stage_done("search_1", "deepseek", 500, reliability={"confirmed": 5, "reliable": True})

    emit_stage_start("synthesis", query, platform)
    time.sleep(0.1)
    emit_stage_done("synthesis", platform, content_len)

    os.makedirs(DATA_DIR, exist_ok=True)
    report_path = os.path.join(DATA_DIR, "_mock_result.md")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write(mock.get("report", f"# Mock 整合报告\n\n{content}"))

    emit_done(elapsed_sec=2.3, report_path=report_path, content_len=content_len)
    return {"status": "done", "report_path": report_path, "content_len": content_len}


def _mock_synthesis_empty(query, mock, depth):
    """模拟整合平台空回复。"""
    platform = mock.get("platform", "kimi")

    emit_stage_start("search_1", query, "deepseek")
    time.sleep(0.05)
    emit_stage_done("search_1", "deepseek", 500, reliability={"confirmed": 5, "reliable": True})

    emit_stage_start("synthesis", query, platform)
    time.sleep(0.1)
    emit_stage_done("synthesis", platform, 0, status="empty",
                    error="整合平台返回空内容")

    emit_error("synthesis_empty", platform=platform, detail="整合平台无输出")
    return {"status": "synthesis_empty"}


# ============================================================
# 三线程模型
# ============================================================

# 线程退出信号（主线程退出前设置，避免 daemon 线程在 shutdown 时崩溃）
_shutdown_event = threading.Event()


def _stdin_listener():
    """stdin 监听线程：读取 C++ 控制指令。

    同时支持长度前缀帧和简易行 JSON：
    - 先尝试按长度前缀帧读取（read_frame）
    - 异常时回退到行模式（read_line_frame）
    """
    from agent_protocol import pipe_broken, stdin_thread_died as stdin_died_flag
    print("[Thread] stdin 监听线程启动", file=sys.stderr)
    try:
        # 使用长度前缀帧读取（与 C++ 发送格式一致）
        from agent_protocol import read_frame as read_prefix_frame
        for frame in read_prefix_frame(sys.stdin.buffer):
            if _shutdown_event.is_set() or pipe_broken.is_set():
                print("[Thread] 退出信号/管道破裂，stdin 监听退出", file=sys.stderr)
                break
            action = frame.get("action", "")
            if action == "cancel":
                cancel_flag.set()
                print("[Ctrl] 收到取消指令", file=sys.stderr)
            elif action == "ping":
                ping_received.set()
            elif action == "hello":
                print("[Ctrl] 忽略重复 hello", file=sys.stderr)
            else:
                # 扩展指令路由（缓存系统等）
                try:
                    from cache import try_handle
                    from agent_protocol import write_frame as _wf
                    if not try_handle(frame, _wf):
                        print(f"[Ctrl] 未知指令: {action}", file=sys.stderr)
                except ImportError:
                    print(f"[Ctrl] 未知指令: {action}", file=sys.stderr)
    except Exception as e:
        print(f"[Thread] stdin 监听线程退出: {e}", file=sys.stderr)
    finally:
        stdin_died_flag.set()
        print("[Thread] stdin 监听线程已退出", file=sys.stderr)


def _heartbeat():
    """heartbeat 线程：独立回 pong，不阻塞。

    检测 pipe_broken、shutdown 信号和 stdin 监听线程死亡，提前退出。
    """
    from agent_protocol import pipe_broken, stdin_thread_died as stdin_died_flag
    print("[Thread] heartbeat 线程启动", file=sys.stderr)
    while not _shutdown_event.is_set():
        if pipe_broken.is_set():
            print("[Heartbeat] 管道破裂 → 退出", file=sys.stderr)
            break
        if stdin_died_flag.is_set():
            print("[Heartbeat] stdin 监听已死 → 退出", file=sys.stderr)
            break
        if ping_received.wait(timeout=1.0):
            ping_received.clear()
            write_frame("pong")
            if pipe_broken.is_set():
                print("[Heartbeat] pong 写入失败(管道破裂) → 退出", file=sys.stderr)
                break
            print("[Heartbeat] pong", file=sys.stderr)


# ============================================================
# Protocol 模式入口
# ============================================================


def run_protocol_mode():
    """Protocol 模式：作为 C++ 子进程运行。

    1. 等待 C++ 发 hello
    2. 提取 config → set_runtime_config()
    3. 启动 stdin 监听 + heartbeat 线程
    4. 运行搜索（或 Mock CDP 模式）
    5. 推 done → 退出
    """
    # 启动握手
    config, mode = handshake(SUPPORTED_PROTOCOL)
    set_runtime_config(config)

    query = config.get("query", "")
    depth = config.get("depth", "L2")
    search_platform = config.get("search_platform", "deepseek")
    synthesis_platform = config.get("synthesis_platform", "deepseek")
    data_dir = config.get("data_dir", DATA_DIR)
    mock_cdp = config.get("mock_cdp", False)
    mock_file = config.get("mock_file", None)

    print(f"[Agent] Protocol 模式 | query={query[:50]} | depth={depth} | "
          f"search={search_platform} synth={synthesis_platform}"
          f"{' | MockCDP' if mock_cdp else ''}", file=sys.stderr)

    if not query:
        emit_error("missing_query", platform="system", detail="hello 中缺少 query")
        sys.exit(1)

    # 启动 stdin 监听线程
    stdin_thread = threading.Thread(target=_stdin_listener, daemon=True)
    stdin_thread.start()

    # 启动 heartbeat 线程
    heartbeat_thread = threading.Thread(target=_heartbeat, daemon=True)
    heartbeat_thread.start()

    # 主线程：执行搜索（或 Mock CDP 模式）
    try:
        if mock_cdp:
            result = run_mock_search(query, mock_file, depth)
            elapsed = 0.3
        else:
            result = agent_search(
                query,
                depth=depth,
                force_platform=search_platform,
                output_dir=data_dir,
            )
            elapsed = result.get("elapsed_sec", 0)

        report_path = result.get("report_path", "")
        execution = result.get("execution") or {}
        total_len = (result.get("content_len", 0) if mock_cdp else
                     sum(r.get("content_len", 0)
                         for r in execution.get("results", [])))

        # 处理 L1 路径（agent_search 提前返回）
        if result.get("note"):
            print(f"[Agent] {result['note']}", file=sys.stderr)
            emit_done(elapsed_sec=0, report_path="", content_len=0)
            return

        # 在 emit_done 前直接写入缓存，消除 C++ 回写 cache_store 的退出竞态
        if not mock_cdp and report_path and os.path.exists(report_path):
            try:
                from cache import get_store
                store = get_store()
                if store:
                    with open(report_path, "r", encoding="utf-8") as f:
                        report_text = f.read()
                    store.store_report(result.get("query", query), report_text, {
                        "project": "",
                        "platform": search_platform,
                        "depth": depth,
                        "report_path": report_path,
                        "content_len": total_len,
                        "elapsed_sec": elapsed,
                    })
            except Exception as cache_e:
                print(f"[Agent] 缓存存储跳过 (不阻断): {cache_e}", file=sys.stderr)

        # mock_cdp 模式下 done 事件已由 mock 函数内部推送，不重复
        if not mock_cdp:
            emit_done(elapsed_sec=elapsed, report_path=report_path, content_len=total_len)

    except Exception as e:
        # ConfigError 已在 execute() 中 emit 过，不重复发送 execution_failed
        from orchestrator import ConfigError
        if not isinstance(e, ConfigError):
            emit_error("execution_failed", platform="system", detail=str(e))
        sys.exit(1)
    finally:
        # 清理：通知 daemon 线程退出，避免 _enter_buffered_busy 崩溃
        _shutdown_event.set()
        print("[Agent] 退出清理（已通知子线程退出）", file=sys.stderr)
        try:
            sys.stdout.flush()
        except Exception:
            pass
        # 给 stdin 监听线程 0.5s 退出，避免 interpreter shutdown 时锁冲突
        time.sleep(0.5)


# ============================================================
# CLI 模式（向后兼容）
# ============================================================


def run_cli_mode(args):
    """CLI 模式：python agent.py "query" [--depth L2] ..."""
    query = args.query
    if args.file:
        with open(args.file, "r", encoding="utf-8") as f:
            query = f.read().strip()
    if not query:
        print("用法: python agent.py '搜索问题' [--depth L2] [--platform deepseek] [--quick] [--mock-cdp]")
        sys.exit(1)

    if args.mock_cdp:
        mock_file = args.mock_file or None
        return run_mock_search(query, mock_file, args.depth)

    result = agent_search(
        query,
        depth=args.depth,
        force_platform=args.platform,
        quick=args.quick,
    )
    print(f"\n{'='*50}")
    tool = result['tool_choice']['tool']
    depth = result['plan']['depth'] if result['plan'] else 'L1'
    elapsed = result.get('elapsed_sec', '?')
    print(f"工具: {tool} | 深度: {depth} | 耗时: {elapsed}s")
    if result.get("report_path"):
        print(f"报告: {result['report_path']}")
    if result.get("note"):
        print(f"说明: {result['note']}")


# ============================================================
# 主入口
# ============================================================


def main():
    """判断运行模式：Protocol（C++ 子进程）/ CLI / Mock CDP。"""
    # 检测 Protocol 模式：C++ 通过 subprocess 启动，stdin 第一个输入是 hello
    # CLI 模式下用户通过命令行参数传入 query
    # 判断依据：有命令行参数 → CLI；无参数 → Protocol（等 C++ hello）
    parser = argparse.ArgumentParser(description="Certus Python Agent")
    parser.add_argument("query", nargs="?", default="", help="搜索问题")
    parser.add_argument("--depth", "-d", default="L2", choices=["L1", "L2", "L3"])
    parser.add_argument("--platform", "-p", default=None, help="强制指定平台")
    parser.add_argument("--quick", "-q", action="store_true", help="快速模式")
    parser.add_argument("--file", "-f", default=None, help="从文件读取问题")
    parser.add_argument("--mock-cdp", action="store_true", help="Mock CDP 模式（无浏览器测试）")
    parser.add_argument("--mock-file", default=None, help="指定 mock JSON 文件")
    parser.add_argument("--protocol", action="store_true", help="强制 Protocol 模式")

    args = parser.parse_args()

    # 判断模式
    # Protocol 模式：显式指定 --protocol 或 stdin 是管道（非终端）
    is_pipe = not sys.stdin.isatty() if hasattr(sys.stdin, "isatty") else False
    if args.protocol or (is_pipe and not args.query and not args.file and not args.mock_cdp):
        run_protocol_mode()
    elif args.query or args.file or args.mock_cdp:
        run_cli_mode(args)
    else:
        parser.print_help()
        print("\n用法: python agent.py '搜索问题' [选项]", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    # 确保 stdout/stderr 使用 UTF-8
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding='utf-8')
        sys.stderr.reconfigure(encoding='utf-8')
    main()
