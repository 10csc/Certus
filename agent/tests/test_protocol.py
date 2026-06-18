# -*- coding: utf-8 -*-
"""协议帧格式测试 —— 验证 4 字节长度前缀 + JSON 帧的编码/解码。"""

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


class TestWriteFrame:
    """帧写入测试。"""

    def setup_method(self):
        """每个测试前重置状态。"""
        proto._seq_counter = 0
        self.mock_stdout = _MockStdout()
        self._old_stdout = sys.stdout
        sys.stdout = self.mock_stdout

    def teardown_method(self):
        sys.stdout = self._old_stdout

    @property
    def buf(self):
        return self.mock_stdout.buffer

    def test_frame_format(self):
        """验证帧格式：4 字节大端长度 + JSON + 换行。"""
        proto.write_frame("test_event", key="value")

        self.buf.seek(0)
        # 读 4 字节长度前缀
        prefix = self.buf.read(4)
        assert len(prefix) == 4

        data_len = struct.unpack(">I", prefix)[0]
        assert data_len > 0

        # 读 JSON 数据
        data = self.buf.read(data_len)
        frame = json.loads(data.decode("utf-8"))

        assert frame["event"] == "test_event"
        assert frame["key"] == "value"
        assert "seq" in frame
        assert "timestamp" in frame

    def test_seq_increment(self):
        """验证 seq 自增。"""
        proto.write_frame("e1")
        proto.write_frame("e2")
        proto.write_frame("e3")

        self.buf.seek(0)
        frames = _read_all_frames(self.buf)

        assert frames[0]["seq"] == 1
        assert frames[1]["seq"] == 2
        assert frames[2]["seq"] == 3

    def test_timestamp_present(self):
        """验证 timestamp 字段存在且为合理值。"""
        import time
        before = time.time()
        proto.write_frame("test")
        after = time.time()

        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        ts = frames[0]["timestamp"]

        assert before <= ts <= after + 0.1

    def test_required_fields(self):
        """验证必填字段：event, seq, timestamp。"""
        proto.write_frame("stage_start", stage="search_1", question="test?")

        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        f = frames[0]

        assert "event" in f
        assert f["event"] == "stage_start"
        assert "seq" in f
        assert isinstance(f["seq"], int)
        assert "timestamp" in f
        assert isinstance(f["timestamp"], float)
        assert f["stage"] == "search_1"
        assert f["question"] == "test?"

    def test_on_event_callback(self):
        """验证 on_event 回调签名和功能。"""
        proto.on_event("stage_done", stage="search_1", content_len=100)

        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        assert frames[0]["event"] == "stage_done"
        assert frames[0]["content_len"] == 100


class TestReadLineFrame:
    """简易行帧读取测试。"""

    def test_read_single_frame(self):
        """读取单个 JSON 帧。"""
        stream = io.StringIO('{"action":"cancel","seq":1}\n')
        frames = list(proto.read_line_frame(stream))
        assert len(frames) == 1
        assert frames[0]["action"] == "cancel"

    def test_read_multiple_frames(self):
        """读取多个 JSON 帧。"""
        stream = io.StringIO(
            '{"action":"cancel","seq":1}\n'
            '{"action":"ping","seq":2}\n'
        )
        frames = list(proto.read_line_frame(stream))
        assert len(frames) == 2
        assert frames[0]["action"] == "cancel"
        assert frames[1]["action"] == "ping"

    def test_skip_empty_lines(self):
        """跳过空行。"""
        stream = io.StringIO('\n\n{"action":"ping"}\n\n')
        frames = list(proto.read_line_frame(stream))
        assert len(frames) == 1

    def test_skip_invalid_json(self):
        """跳过无效 JSON 行。"""
        stream = io.StringIO('not json\n{"action":"ping"}\n')
        frames = list(proto.read_line_frame(stream))
        assert len(frames) == 1
        assert frames[0]["action"] == "ping"

    def test_empty_stream(self):
        """空流返回空列表。"""
        stream = io.StringIO('')
        frames = list(proto.read_line_frame(stream))
        assert len(frames) == 0


class TestReadFrame:
    """长度前缀帧读取测试。"""

    def _make_frame(self, data_dict):
        """创建长度前缀帧的字节流。"""
        raw = json.dumps(data_dict, ensure_ascii=False).encode("utf-8")
        return struct.pack(">I", len(raw)) + raw + b"\n"

    def test_read_single_frame(self):
        """读取单个长度前缀帧。"""
        raw_stream = io.BytesIO(self._make_frame({"action": "cancel", "seq": 1}))
        # read_frame 需要 .read() 方法，BytesIO 支持
        frames = list(proto.read_frame(raw_stream))
        assert len(frames) == 1
        assert frames[0]["action"] == "cancel"

    def test_read_multiple_frames(self):
        """读取多个长度前缀帧。"""
        data = (
            self._make_frame({"action": "cancel", "seq": 1}) +
            self._make_frame({"action": "ping", "seq": 2})
        )
        raw_stream = io.BytesIO(data)
        frames = list(proto.read_frame(raw_stream))
        assert len(frames) == 2
        assert frames[0]["action"] == "cancel"
        assert frames[1]["action"] == "ping"


class TestConvenienceFunctions:
    """便捷函数测试。"""

    def setup_method(self):
        proto._seq_counter = 0
        self.mock_stdout = _MockStdout()
        self._old_stdout = sys.stdout
        sys.stdout = self.mock_stdout

    def teardown_method(self):
        sys.stdout = self._old_stdout

    @property
    def buf(self):
        return self.mock_stdout.buffer

    def test_emit_stage_start(self):
        """emit_stage_start 产生正确的事件帧。"""
        proto.emit_stage_start("search_1", "测试问题?", "deepseek")
        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        assert frames[0]["event"] == "stage_start"
        assert frames[0]["stage"] == "search_1"
        assert frames[0]["platform"] == "deepseek"

    def test_emit_stage_done(self):
        """emit_stage_done 产生正确的事件帧。"""
        proto.emit_stage_done("search_1", "deepseek", 500, reliability={"reliable": True})
        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        assert frames[0]["event"] == "stage_done"
        assert frames[0]["content_len"] == 500
        assert frames[0]["reliability"]["reliable"] is True

    def test_emit_done(self):
        """emit_done 产生正确的事件帧。"""
        proto.emit_done(120, "/path/to/report.md", 10000)
        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        assert frames[0]["event"] == "done"
        assert frames[0]["elapsed_sec"] == 120

    def test_emit_error(self):
        """emit_error 产生正确的错误事件。"""
        proto.emit_error("timeout", "deepseek", "超过 180s")
        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        assert frames[0]["event"] == "error"
        assert frames[0]["error_type"] == "timeout"

    def test_emit_cancelled(self):
        """emit_cancelled 产生正确的取消事件。"""
        proto.emit_cancelled("已收集部分内容...")
        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        assert frames[0]["event"] == "cancelled"

    def test_event_enum_values(self):
        """验证事件类型枚举值正确。"""
        valid_events = {
            "hello_ack", "stage_start", "stage_done",
            "evolution_start", "evolution_done", "evolution_failed",
            "cancelled", "pong", "done", "error",
        }
        proto.write_frame("stage_start")
        self.buf.seek(0)
        frames = _read_all_frames(self.buf)
        assert frames[0]["event"] in valid_events


# ============================================================
# 辅助函数
# ============================================================


def _read_all_frames(stream):
    """从字节流中读取所有长度前缀帧。"""
    frames = []
    stream.seek(0)
    while True:
        prefix = stream.read(4)
        if not prefix or len(prefix) < 4:
            break
        data_len = struct.unpack(">I", prefix)[0]
        data = stream.read(data_len)
        if not data:
            break
        # 跳过换行
        stream.read(1)
        frames.append(json.loads(data.decode("utf-8")))
    return frames
