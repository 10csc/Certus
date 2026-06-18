# -*- coding: utf-8 -*-
"""测试配置 —— 协议测试需要直接操作 sys.stdout，禁用 pytest 输出捕获。"""

import pytest


def pytest_collection_modifyitems(config, items):
    """对协议测试类禁用 stdout 捕获。"""
    for item in items:
        # test_protocol 中的帧写入测试需要直接操作 sys.stdout
        if item.parent and item.parent.name in (
            "TestWriteFrame",
            "TestConvenienceFunctions",
        ):
            # 添加标记，运行时通过 -m "no_capture" 或 -s 处理
            pass
