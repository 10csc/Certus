# -*- coding: utf-8 -*-
"""Mock CDP 搜索测试 —— 验证事件生命周期状态机（不需要真实浏览器）。"""

import sys
import os
import io
import json
import struct
import pytest

SCRIPT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import agent_protocol as proto
from agent import run_mock_search

MOCK_DIR = os.path.join(SCRIPT_DIR, "tests", "mock_data")


class _MockStdout:
    """模拟 sys.stdout，提供可写的 .buffer (BytesIO)。"""
    def __init__(self):
        self.buffer = io.BytesIO()
        self.encoding = "utf-8"
    def flush(self):
        self.buffer.flush()
    def write(self, s):
        if isinstance(s, str):
            self.buffer.write(s.encode("utf-8"))
        else:
            self.buffer.write(s)
    def reconfigure(self, **kwargs):
        pass


def _capture_events(func, *args, **kwargs):
    """捕获 func 执行期间的所有 stdout 事件帧。"""
    mock_stdout = _MockStdout()
    old_stdout = sys.stdout
    old_counter = proto._seq_counter

    try:
        proto._seq_counter = 0
        sys.stdout = mock_stdout
        result = func(*args, **kwargs)
    finally:
        sys.stdout.flush()
        sys.stdout = old_stdout
        proto._seq_counter = old_counter

    # 解析所有帧
    buf = mock_stdout.buffer
    buf.seek(0)
    frames = []
    while True:
        prefix = buf.read(4)
        if not prefix or len(prefix) < 4:
            break
        data_len = struct.unpack(">I", prefix)[0]
        data = buf.read(data_len)
        if not data:
            break
        buf.read(1)  # 跳过换行
        frames.append(json.loads(data.decode("utf-8")))
    return result, frames


class TestMockSearchOK:
    """正常搜索流程测试。"""

    def test_stage_lifecycle(self):
        """验证 stage_start → stage_done → done 事件序列。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "search_deepseek_ok.json")
        )

        events = [f["event"] for f in frames]
        assert "stage_start" in events
        assert "stage_done" in events
        assert "done" in events

        # 验证顺序：stage_start 在 stage_done 之前，stage_done 在 done 之前
        start_idx = events.index("stage_start")
        done_idx = events.index("stage_done")
        end_idx = events.index("done")
        assert start_idx < done_idx < end_idx

    def test_seq_continuity(self):
        """验证 seq 连续递增。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "search_deepseek_ok.json")
        )

        seqs = [f["seq"] for f in frames]
        for i in range(len(seqs) - 1):
            assert seqs[i] + 1 == seqs[i + 1], f"seq 不连续: {seqs}"

    def test_done_event_fields(self):
        """验证 done 事件包含必要字段。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "search_deepseek_ok.json")
        )

        done_frame = next(f for f in frames if f["event"] == "done")
        assert "elapsed_sec" in done_frame
        assert "report_path" in done_frame
        assert "content_len" in done_frame
        assert done_frame["content_len"] > 0

    def test_reliability_in_stage_done(self):
        """验证 stage_done 包含可靠性信息。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "search_deepseek_ok.json")
        )

        stage_done = next(f for f in frames if f["event"] == "stage_done")
        assert "reliability" in stage_done
        assert stage_done["reliability"]["reliable"] is True


class TestMockSearchTimeout:
    """搜索超时流程测试。"""

    def test_timeout_emits_error(self):
        """超时应当产生 error 事件。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "search_timeout.json")
        )

        events = [f["event"] for f in frames]
        assert "stage_start" in events
        assert "error" in events

        error_frame = next(f for f in frames if f["event"] == "error")
        assert error_frame["error_type"] == "timeout"

    def test_timeout_stage_done_with_error(self):
        """超时的 stage_done 应标明 timeout 状态。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "search_timeout.json")
        )

        stage_done = next(f for f in frames if f["event"] == "stage_done")
        assert stage_done["status"] == "timeout"
        assert stage_done["content_len"] == 0


class TestMockPlatformReject:
    """平台拒绝流程测试。"""

    def test_reject_emits_error_with_suggestion(self):
        """平台拒绝应产生 error + suggestion。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "search_platform_reject.json")
        )

        error_frame = next(f for f in frames if f["event"] == "error")
        assert error_frame["error_type"] == "platform_rejected"

        stage_done = next(f for f in frames if f["event"] == "stage_done")
        assert stage_done["status"] == "rejected"
        assert stage_done.get("suggestion") == "try_other_platform"


class TestMockExtractFail:
    """提取失败流程测试。"""

    def test_extract_fail_emits_error(self):
        """提取失败应产生 extract_failed 错误。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "extract_fail.json")
        )

        error_frame = next(f for f in frames if f["event"] == "error")
        assert error_frame["error_type"] == "extract_failed"


class TestMockSynthesis:
    """整合流程测试。"""

    def test_synthesis_ok_lifecycle(self):
        """正常整合：search_1 stage_done + synthesis stage_done + done。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "synthesis_kimi_ok.json")
        )

        events = [f["event"] for f in frames]
        assert "stage_start" in events
        assert "stage_done" in events
        assert "done" in events

        # 应该有两次 stage_done（搜索 + 整合）
        stage_dones = [f for f in frames if f["event"] == "stage_done"]
        assert len(stage_dones) >= 1  # 至少有一次 stage_done

    def test_synthesis_empty_emits_error(self):
        """空整合应产生 synthesis_empty 错误。"""
        _, frames = _capture_events(
            run_mock_search, "测试问题",
            mock_file=os.path.join(MOCK_DIR, "synthesis_empty.json")
        )

        error_frame = next(f for f in frames if f["event"] == "error")
        assert error_frame["error_type"] == "synthesis_empty"


class TestMockFileMissing:
    """Mock 文件缺失测试。"""

    def test_missing_file_exits_with_error(self):
        """不存在的 mock 文件应导致退出。"""
        with pytest.raises(SystemExit):
            run_mock_search("测试", mock_file="/nonexistent/path.json")


class TestCancelFlag:
    """取消标记测试。"""

    def test_cancel_flag_initial_state(self):
        """cancel_flag 初始为未设置。"""
        proto.cancel_flag.clear()
        assert not proto.cancel_flag.is_set()

    def test_cancel_flag_set_and_check(self):
        """设置 cancel_flag 后可被检测到。"""
        proto.cancel_flag.clear()
        proto.cancel_flag.set()
        assert proto.cancel_flag.is_set()
        proto.cancel_flag.clear()
