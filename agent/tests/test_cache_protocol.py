# -*- coding: utf-8 -*-
"""缓存协议处理器单元测试。"""

import os
import sys
import json
import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from cache.store import CacheStore
from cache.protocol_handler import CacheProtocolHandler


@pytest.fixture
def store(tmp_path):
    return CacheStore(persist_dir=str(tmp_path / "chroma_proto"))


@pytest.fixture
def handler(store):
    return CacheProtocolHandler(store)


@pytest.fixture
def report_file(tmp_path):
    """创建临时报告文件。"""
    path = tmp_path / "test_report.md"
    path.write_text("# 测试报告\n\n这是 asyncio 并发模型的研究结果。\n", encoding="utf-8")
    return str(path)


class TestCacheQuery:
    """cache_query 指令测试。"""

    def test_empty_query_returns_miss(self, handler):
        frames = []
        handler.handle({"action": "cache_query", "query": ""}, lambda *a, **kw: frames.append((a, kw)))
        assert len(frames) == 1
        assert frames[0][0][0] == "cache_miss"

    def test_no_cache_returns_miss(self, handler):
        frames = []
        handler.handle(
            {"action": "cache_query", "query": "任意查询"},
            lambda *a, **kw: frames.append((a, kw))
        )
        assert len(frames) == 1
        assert frames[0][0][0] == "cache_miss"

    def test_cached_query_returns_hit(self, handler, store, report_file):
        # 先存入
        with open(report_file, "r", encoding="utf-8") as f:
            text = f.read()
        store.store_report("Python asyncio 并发模型研究报告", text, {"project": "test"})

        frames = []
        handler.handle(
            {"action": "cache_query", "query": "Python asyncio 并发模型研究报告", "min_similarity": 0.5},
            lambda *a, **kw: frames.append((a, kw))
        )
        assert len(frames) == 1
        assert frames[0][0][0] == "cache_hit"


class TestCacheStore:
    """cache_store 指令测试。"""

    def test_store_writes_to_cache(self, handler, store, report_file):
        frames = []
        handler.handle(
            {
                "action": "cache_store",
                "query": "测试报告",
                "report_path": report_file,
                "project": "test",
                "platform": "deepseek",
            },
            lambda *a, **kw: frames.append((a, kw))
        )
        assert len(frames) == 1
        assert frames[0][0][0] == "cache_stored"
        assert store.stats()["total_reports"] == 1

    def test_store_missing_params_no_crash(self, handler):
        frames = []
        handler.handle(
            {"action": "cache_store", "query": "", "report_path": ""},
            lambda *a, **kw: frames.append((a, kw))
        )
        # 不应崩溃，只是静默忽略

    def test_store_bad_file_no_crash(self, handler):
        frames = []
        handler.handle(
            {"action": "cache_store", "query": "test", "report_path": "/nonexistent/file.md"},
            lambda *a, **kw: frames.append((a, kw))
        )
        # 不应崩溃


class TestKnowledgeSearch:
    """knowledge_search 指令测试。"""

    def test_empty_query_returns_empty(self, handler):
        frames = []
        handler.handle(
            {"action": "knowledge_search", "query": ""},
            lambda *a, **kw: frames.append((a, kw))
        )
        assert len(frames) == 1
        assert frames[0][0][0] == "knowledge_search_results"
        assert frames[0][1].get("results") == []

    def test_sync_then_search(self, handler, store):
        store.sync_knowledge(1, "Python 异步编程", "asyncio 是标准异步框架")

        frames = []
        handler.handle(
            {"action": "knowledge_search", "query": "异步框架"},
            lambda *a, **kw: frames.append((a, kw))
        )
        assert len(frames) == 1
        results = frames[0][1].get("results", [])
        assert len(results) >= 1


class TestUnknownAction:
    """未知指令不应被处理。"""

    def test_unknown_returns_false(self, handler):
        result = handler.handle(
            {"action": "unknown_action"},
            lambda *a, **kw: None
        )
        assert result is False
