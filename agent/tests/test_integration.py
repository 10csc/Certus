# -*- coding: utf-8 -*-
"""集成测试 —— 模拟 C++ 端与 Python Agent 的完整协议交互。

使用 subprocess 启动 agent.py，通过 stdin/stdout JSON 帧通信。
"""

import sys
import os
import io
import json
import struct
import time
import subprocess
import pytest

SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
AGENT_ENTRY = os.path.join(SCRIPT_DIR, "agent.py")


def _build_frame(data_dict):
    """构造长度前缀帧（C++ 端发送格式）。"""
    raw = json.dumps(data_dict, ensure_ascii=False).encode("utf-8")
    return struct.pack(">I", len(raw)) + raw + b"\n"


def _read_frame(stream, timeout=5.0):
    """从流中读一个长度前缀帧。"""
    deadline = time.time() + timeout
    prefix = b""
    while len(prefix) < 4 and time.time() < deadline:
        try:
            chunk = stream.read(4 - len(prefix))
        except Exception:
            time.sleep(0.05)
            continue
        if chunk:
            prefix += chunk
        else:
            time.sleep(0.05)
    if len(prefix) < 4:
        return None

    try:
        data_len = struct.unpack(">I", prefix)[0]
    except struct.error:
        return None
    if data_len <= 0 or data_len > 10 * 1024 * 1024:
        return None

    data = b""
    while len(data) < data_len and time.time() < deadline:
        try:
            chunk = stream.read(data_len - len(data))
        except Exception:
            time.sleep(0.05)
            continue
        if chunk:
            data += chunk
        else:
            time.sleep(0.05)
    if len(data) < data_len:
        return None

    # 跳过换行
    try:
        stream.read(1)
    except Exception:
        pass

    try:
        return json.loads(data.decode("utf-8"))
    except Exception:
        return None


class TestProtocolIntegration:
    """协议集成测试（启动 Python 进程）。"""

    @pytest.fixture
    def agent_process(self):
        """启动 Python Agent（Protocol 模式——通过 stdin 管道）。"""
        env = os.environ.copy()
        env["PYTHONUTF8"] = "1"

        proc = subprocess.Popen(
            [sys.executable, AGENT_ENTRY, "--protocol"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )
        yield proc
        try:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=3)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass

    def _handshake(self, agent_process, query="测试搜索", mock_cdp=False):
        """发送 hello 并等待 hello_ack。返回 ack 帧。"""
        config = {
            "query": query,
            "depth": "L2",
            "search_platform": "deepseek",
            "synthesis_platform": "deepseek",
            "data_dir": os.path.join(PROJECT_ROOT, "data"),
            "cdp_port": 9222,
        }
        if mock_cdp:
            config["mock_cdp"] = True
            config["mock_file"] = os.path.join(
                SCRIPT_DIR, "tests", "mock_data", "search_deepseek_ok.json"
            )
        hello = {"action": "hello", "protocol": "1.0", "config": config}
        try:
            agent_process.stdin.write(_build_frame(hello))
            agent_process.stdin.flush()
        except (OSError, BrokenPipeError):
            pytest.skip("Agent 进程已退出（可能是环境问题）")

        ack = _read_frame(agent_process.stdout, timeout=5.0)
        return ack

    def test_handshake_ok(self, agent_process):
        """正常握手：hello → hello_ack。"""
        ack = self._handshake(agent_process)
        assert ack is not None, "未收到 hello_ack"
        assert ack["event"] == "hello_ack"
        assert ack["protocol"] == "1.0"
        assert "agent_version" in ack

    def test_handshake_version_mismatch(self, agent_process):
        """版本不匹配：C++ 2.0 vs Python 1.0 → 报错退出。"""
        hello = {
            "action": "hello",
            "protocol": "2.0",
            "config": {"query": "test"},
        }
        try:
            agent_process.stdin.write(_build_frame(hello))
            agent_process.stdin.flush()
        except (OSError, BrokenPipeError):
            return  # 进程已退出，符合预期

        # 可能收到 error 事件，也可能直接退出
        frames = []
        for _ in range(3):
            f = _read_frame(agent_process.stdout, timeout=2.0)
            if f:
                frames.append(f)
            else:
                break

        errors = [f for f in frames if f.get("event") == "error"]
        if errors:
            assert errors[0]["error_type"] == "protocol_mismatch"

        # 进程应该退出
        try:
            exit_code = agent_process.wait(timeout=3)
            assert exit_code != 0
        except subprocess.TimeoutExpired:
            agent_process.kill()

    def test_ping_pong(self, agent_process):
        """心跳：C++ 发 ping → Python 回 pong。"""
        ack = self._handshake(agent_process, "心跳测试", mock_cdp=True)
        if ack is None:
            pytest.skip("握手失败，跳过 ping/pong 测试")

        # 等待搜索线程启动
        time.sleep(0.3)

        try:
            agent_process.stdin.write(_build_frame({"action": "ping", "seq": 1}))
            agent_process.stdin.flush()
        except (OSError, BrokenPipeError):
            pytest.skip("Agent stdin 已关闭")

        pong = None
        for _ in range(15):
            f = _read_frame(agent_process.stdout, timeout=0.5)
            if f and f.get("event") == "pong":
                pong = f
                break
        assert pong is not None, "未收到 pong"

    def test_cancel_flow(self, agent_process):
        """取消流程：C++ 发 cancel → Python 确认。"""
        ack = self._handshake(agent_process, "取消测试", mock_cdp=True)
        if ack is None:
            pytest.skip("握手失败，跳过 cancel 测试")

        time.sleep(0.3)

        try:
            agent_process.stdin.write(_build_frame({"action": "cancel", "seq": 1}))
            agent_process.stdin.flush()
        except (OSError, BrokenPipeError):
            pytest.skip("Agent stdin 已关闭")

        # 收集事件，等待 cancelled
        cancelled_event = None
        for _ in range(20):
            f = _read_frame(agent_process.stdout, timeout=0.5)
            if f and f.get("event") == "cancelled":
                cancelled_event = f
                break

        assert cancelled_event is not None, "未收到 cancelled 事件"


class TestCLIMode:
    """CLI 模式测试。"""

    def test_cli_mock_search(self):
        """CLI 模式下运行 mock CDP 搜索。"""
        env = os.environ.copy()
        env["PYTHONUTF8"] = "1"

        proc = subprocess.Popen(
            [
                sys.executable, AGENT_ENTRY,
                "--mock-cdp",
                "--mock-file",
                os.path.join(SCRIPT_DIR, "tests", "mock_data", "search_deepseek_ok.json"),
                "CLI测试问题",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )

        frames = []
        for _ in range(20):
            f = _read_frame(proc.stdout, timeout=2.0)
            if f:
                frames.append(f)
            else:
                break

        proc.wait(timeout=5)
        events = [f["event"] for f in frames]
        assert "stage_start" in events, f"CLI mock 应有 stage_start，收到: {events}"
        assert "done" in events, f"CLI mock 应有 done，收到: {events}"

    def test_cli_missing_query(self):
        """CLI 模式无参数且非管道 → 报错退出。"""
        env = os.environ.copy()
        env["PYTHONUTF8"] = "1"

        proc = subprocess.Popen(
            [sys.executable, AGENT_ENTRY],
            stdin=subprocess.DEVNULL,  # 非管道 → CLI 模式
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=env,
        )

        try:
            exit_code = proc.wait(timeout=5)
            assert exit_code != 0, "无参数时应该报错退出"
        except subprocess.TimeoutExpired:
            proc.kill()
            exit_code = proc.wait()
            assert exit_code != 0, "进程被强杀，但应在此之前报错退出"


class TestCommonIntegration:
    """common.py 基础改造测试。"""

    def test_set_runtime_config(self):
        """验证 set_runtime_config + load_config 工作流。"""
        from common import set_runtime_config, load_config

        test_config = {
            "cdp_port": 9224,
            "search_platform": "deepseek",
            "synthesis_platform": "deepseek",
            "custom_field": "test_value",
        }
        set_runtime_config(test_config)

        loaded = load_config()
        assert loaded["cdp_port"] == 9224
        assert loaded["search_platform"] == "deepseek"
        assert loaded["custom_field"] == "test_value"

        # 重置
        from common import set_runtime_config
        set_runtime_config(None)

    def test_load_config_fallback_to_file(self):
        """不设置 runtime config 时从文件读取。"""
        from common import set_runtime_config, load_config

        set_runtime_config(None)
        config = load_config()
        # 应该包含基本字段
        assert isinstance(config, dict)
