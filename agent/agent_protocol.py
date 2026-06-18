# -*- coding: utf-8 -*-
"""Certus 通信协议 —— 帧格式 + on_event 回调 + 启动握手。

C++ ↔ Python 通过 stdin/stdout JSON 通信。
Python stdout 只走 JSON 事件流，所有日志走 stderr。
"""

import sys
import json
import time
import struct
import threading

# 支持的协议版本
SUPPORTED_PROTOCOL = "1.0"

# 全局自增序号（线程安全）
_seq_lock = threading.Lock()
_seq_counter = 0

# 管道破裂标记：write_frame 失败时设置，heartbeat/agent 检测后退出
pipe_broken = threading.Event()
# stdin 监听线程死亡标记：agent.py 中 _stdin_listener 退出时设置
stdin_thread_died = threading.Event()


def _next_seq():
    """原子自增序号。"""
    global _seq_counter
    with _seq_lock:
        _seq_counter += 1
        return _seq_counter


# ============================================================
# 帧写入（Python → C++）
# ============================================================


def write_frame(event_type, **payload):
    """写一帧 JSON 到 stdout（4 字节大端长度前缀 + JSON + \n）。

    失败时设置 pipe_broken 标记——调用方（heartbeat/agent 主循环）
    应在每次调用后检查 pipe_broken.is_set() 并退出进程。
    线程安全：seq 自增受 Lock 保护。
    """
    try:
        frame = {
            "event": event_type,
            "seq": _next_seq(),
            "timestamp": time.time(),
        }
        frame.update(payload)

        data = json.dumps(frame, ensure_ascii=False).encode("utf-8")
        prefix = struct.pack(">I", len(data))

        with _seq_lock:  # 和 seq 用同一把锁，保证帧写入原子性
            sys.stdout.buffer.write(prefix + data + b"\n")
            sys.stdout.buffer.flush()
    except Exception:
        pipe_broken.set()
        try:
            print(
                f"[PROTOCOL] write_frame 失败 (管道破裂): event={event_type}",
                file=sys.stderr,
            )
        except Exception:
            pass


def on_event(event_type, **payload):
    """on_event 回调（供 orchestrator.execute() 使用）。

    用法：
        orchestrator.execute(plan_dict, on_event=on_event)
    """
    write_frame(event_type, **payload)


# ============================================================
# 帧读取（C++ → Python）
# ============================================================


def read_frame(stream):
    """从流中逐帧读取 JSON。生成器，每次 yield 一个 dict。

    格式：4 字节大端长度前缀 → JSON 数据 → 换行。
    用于 stdin 监听线程。

    不可恢复错误（帧头损坏/非法长度）→ sys.exit(1)，
    让 C++ 通过 QProcess::finished 立即感知，而非等心跳超时。
    """
    while True:
        try:
            prefix = stream.read(4)
        except Exception:
            return
        if not prefix or len(prefix) < 4:
            return

        try:
            data_len = struct.unpack(">I", prefix)[0]
        except struct.error:
            print("[PROTOCOL] 帧头解析失败 → 流已损坏，退出进程", file=sys.stderr)
            sys.exit(1)

        if data_len == 0 or data_len > 10 * 1024 * 1024:  # 上限 10MB
            print(f"[PROTOCOL] 非法帧长度: {data_len} → 流已损坏，退出进程", file=sys.stderr)
            sys.exit(1)

        try:
            data = stream.read(data_len)
        except Exception:
            return
        if not data or len(data) < data_len:
            return

        # 跳过尾随换行
        try:
            stream.read(1)
        except Exception:
            pass

        try:
            frame = json.loads(data.decode("utf-8"))
            yield frame
        except (json.JSONDecodeError, UnicodeDecodeError):
            print(f"[PROTOCOL] JSON 解析失败 (帧前80字节): {data[:80]}", file=sys.stderr)
            continue


def read_line_frame(stream):
    """简易帧读取：按行读 JSON（每行一个完整 JSON 对象）。

    兼容模式：用于 C++ 端可能不使用长度前缀的过渡期。
    生成器，每次 yield 一个 dict。
    """
    while True:
        try:
            line = stream.readline()
        except Exception:
            return
        if not line:
            return
        line = line.strip()
        if not line:
            continue
        try:
            frame = json.loads(line)
            yield frame
        except json.JSONDecodeError:
            print(f"[PROTOCOL] JSON 行解析失败: {line[:80]}", file=sys.stderr)
            continue


# ============================================================
# 启动握手
# ============================================================


def handshake(protocol_version=SUPPORTED_PROTOCOL, timeout=10.0):
    """等待 C++ 发 hello → 校验协议版本 → 回 hello_ack → 返回 config dict。

    超时未收到 hello 则报错退出。
    版本不匹配 → 报错退出（exit code 2）。

    返回: config dict（从 hello 中提取）
    """
    # 读取 hello 帧（sys.stdin.buffer.read() 在管道模式下会阻塞等待数据）
    hello = _read_hello_frame(timeout)

    if hello is None:
        print(
            json.dumps({
                "event": "error",
                "seq": _next_seq(),
                "timestamp": time.time(),
                "error_type": "handshake_timeout",
                "detail": f"等待 hello 超时 ({timeout}s)",
            }, ensure_ascii=False),
            file=sys.stderr,
        )
        sys.exit(1)

    if hello.get("action") != "hello":
        print(
            f"[PROTOCOL] 首帧不是 hello: {hello.get('action', '?')}",
            file=sys.stderr,
        )
        sys.exit(1)

    # 校验协议版本
    remote_version = hello.get("protocol", "0.0")
    if remote_version != protocol_version:
        write_frame("error",
                     error_type="protocol_mismatch",
                     detail=f"协议版本不匹配: C++={remote_version} Python={protocol_version}",
                     agent_version=_get_agent_version())
        print(
            f"[PROTOCOL] 协议版本不匹配: C++={remote_version} != Python={protocol_version}",
            file=sys.stderr,
        )
        sys.exit(2)

    # 提取配置
    config = hello.get("config", {})
    mode = hello.get("mode", "search")

    # 回 hello_ack
    write_frame("hello_ack",
                protocol=protocol_version,
                agent_version=_get_agent_version())

    return config, mode


def _get_agent_version():
    """读取 Agent 版本号。"""
    try:
        import os as _os
        version_file = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..", "version.txt")
        if _os.path.exists(version_file):
            with open(version_file, "r", encoding="utf-8") as f:
                return f.read().strip()
    except Exception:
        pass
    return "0.1.0"


def _read_hello_frame(timeout):
    """尝试读取 hello 帧。先试长度前缀帧，再试简易行帧。

    阻塞读取，不设内部超时——超时由 C++ 端 QProcess 生命周期控制。
    C++ 10s 内未收到 hello_ack 会强杀进程，stdin 管道关闭 → read 返回空。
    """
    # 先尝试长度前缀帧读取
    try:
        prefix_bytes = sys.stdin.buffer.read(4)
        if not prefix_bytes or len(prefix_bytes) < 4:
            return None

        data_len = struct.unpack(">I", prefix_bytes)[0]
        if 0 < data_len <= 10 * 1024 * 1024:
            data = sys.stdin.buffer.read(data_len)
            if data and len(data) == data_len:
                # 跳过尾随换行
                try:
                    sys.stdin.buffer.read(1)
                except Exception:
                    pass
                return json.loads(data.decode("utf-8"))
    except (struct.error, UnicodeDecodeError):
        # 帧损坏 → 无法恢复，退出让 C++ 感知
        print("[PROTOCOL] hello 帧解析失败 → 退出", file=sys.stderr)
        sys.exit(1)
    except OSError:
        pass

    # 兜底：简易行读取（用于兼容不使用长度前缀的 C++ 端）
    try:
        line = sys.stdin.readline()
        if line:
            frame = json.loads(line.strip())
            if frame.get("action") == "hello":
                return frame
    except Exception:
        pass

    return None


# ============================================================
# 控制指令处理
# ============================================================

# 取消标记（agent.py 和 orchestrator 共享）
cancel_flag = threading.Event()
# 心跳 ping 标记
ping_received = threading.Event()


def handle_control_frame(frame):
    """处理 C++ 发来的控制指令。在 stdin 监听线程中调用。

    返回 True 表示正常，False 表示流结束。
    """
    action = frame.get("action", "")
    if action == "cancel":
        cancel_flag.set()
        print("[CTRL] 收到取消指令", file=sys.stderr)
    elif action == "ping":
        ping_received.set()
        write_frame("pong")
    else:
        # 未知指令：忽略（向前兼容）
        print(f"[CTRL] 未知指令: {action}", file=sys.stderr)
    return True


# ============================================================
# 便捷函数
# ============================================================


def emit_stage_start(stage, question, platform):
    """推送搜索阶段开始事件。"""
    write_frame("stage_start", stage=stage, question=question, platform=platform)


def emit_stage_done(stage, platform, content_len, **extra):
    """推送搜索阶段完成事件。"""
    payload = {"stage": stage, "platform": platform, "content_len": content_len}
    payload.update(extra)
    write_frame("stage_done", **payload)


def emit_done(elapsed_sec, report_path, content_len=0):
    """推送搜索完成事件。"""
    write_frame("done", elapsed_sec=elapsed_sec, report_path=report_path, content_len=content_len)


def emit_error(error_type, platform="unknown", detail=""):
    """推送错误事件。"""
    write_frame("error", error_type=error_type, platform=platform, detail=str(detail)[:2000])


def emit_cancelled(partial_result=""):
    """推送取消确认事件。"""
    write_frame("cancelled", partial_result=partial_result)


def emit_evolution_start(reason):
    """推送进化开始事件。"""
    write_frame("evolution_start", reason=reason)


def emit_evolution_done(changes, fixed=True):
    """推送进化完成事件。"""
    write_frame("evolution_done", changes=changes, fixed=fixed)


def emit_evolution_failed(error):
    """推送进化失败事件。"""
    write_frame("evolution_failed", error=str(error)[:2000])
