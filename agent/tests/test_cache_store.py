# -*- coding: utf-8 -*-
"""缓存存储引擎单元测试。"""

import os
import sys
import tempfile
import shutil
import pytest

# 确保能导入 cache 模块
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from cache.config import MIN_SIMILARITY, COLLECTION_REPORTS, SUMMARY_MAX_LEN
from cache.store import CacheStore


@pytest.fixture
def store(tmp_path):
    """创建临时 CacheStore 实例。"""
    return CacheStore(persist_dir=str(tmp_path / "chroma_test"))


@pytest.fixture
def sample_report():
    """示例报告文本。"""
    return """# Python asyncio 并发模型研究报告

## 摘要
Python 3.12 的 asyncio 模块提供了基于协程的异步 I/O 框架。
与多线程不同，asyncio 在单线程中通过事件循环实现并发。

## 关键发现
1. asyncio 使用 async/await 语法
2. 事件循环是核心调度器
3. 适合 I/O 密集型任务
4. 2026 年 asyncio 已成为 Python 异步的标准

## 来源
- Python 官方文档
- Real Python 教程
"""


class TestCacheStore:
    """CacheStore 核心功能测试。"""

    def test_store_and_query(self, store, sample_report):
        """存入后能语义搜索命中。"""
        doc_id = store.store_report(
            query="Python asyncio 并发模型",
            report_text=sample_report,
            metadata={"project": "test", "platform": "deepseek", "depth": "L2"},
        )
        assert doc_id.startswith("report_")

        # 完全相同查询应命中
        matches = store.query_cache("Python asyncio 并发模型", project="test")
        assert len(matches) >= 1
        assert matches[0]["query"] == "Python asyncio 并发模型"
        assert matches[0]["similarity"] >= MIN_SIMILARITY

    def test_similar_query_hits(self, store, sample_report):
        """语义相近的查询也能命中。"""
        store.store_report(
            query="Python asyncio 并发编程",
            report_text=sample_report,
            metadata={"project": "test"},
        )

        # 措辞不同但语义相近
        matches = store.query_cache("Python 异步编程 asyncio", project="test")
        assert len(matches) >= 1

    def test_unrelated_query_misses(self, store, sample_report):
        """不相关的查询不命中。"""
        store.store_report(
            query="Python asyncio 并发",
            report_text=sample_report,
            metadata={"project": "test"},
        )

        matches = store.query_cache("Rust 所有权机制详解", project="test")
        assert len(matches) == 0

    def test_project_filter(self, store, sample_report):
        """项目过滤生效。"""
        store.store_report(
            query="Python asyncio 异步框架详细分析",
            report_text=sample_report,
            metadata={"project": "project_a"},
        )

        # 其他项目查不到
        matches = store.query_cache("Python asyncio 异步框架详细分析", project="project_b")
        assert len(matches) == 0

        # 同项目能查到
        matches = store.query_cache("Python asyncio 异步框架详细分析", project="project_a")
        assert len(matches) >= 1

    def test_delete_by_id(self, store, sample_report):
        """删除后查不到。"""
        doc_id = store.store_report(
            query="可删除的报告",
            report_text=sample_report,
            metadata={"project": "test"},
        )

        assert store.delete_by_id(doc_id) is True
        matches = store.query_cache("可删除的报告", project="test")
        assert len(matches) == 0

    def test_stats(self, store, sample_report):
        """统计信息正确。"""
        assert store.stats()["total_reports"] == 0

        store.store_report("报告1", sample_report, {"project": "test"})
        store.store_report("报告2", sample_report, {"project": "test"})
        assert store.stats()["total_reports"] == 2

    def test_summarize_strips_markdown(self, store):
        """摘要去除 Markdown 格式。"""
        text = "# 标题\n\n**加粗** 和 `代码` 和 [链接](url)\n\n---"
        summary = store._summarize(text, max_len=100)
        assert "#" not in summary
        assert "**" not in summary
        assert "`" not in summary

    def test_summarize_truncates(self, store):
        """超长文本截断。"""
        text = "A" * 5000
        summary = store._summarize(text, max_len=2000)
        assert len(summary) == 2000

    def test_empty_collection_query(self, store):
        """空 collection 查询返回空列表。"""
        matches = store.query_cache("任意查询")
        assert matches == []

    def test_min_similarity_threshold(self, store, sample_report):
        """高阈值过滤低相似度结果。"""
        store.store_report(
            query="Python 基础教程",
            report_text="Python 是一种编程语言，由 Guido van Rossum 创建。",
            metadata={"project": "test"},
        )

        # 设置极高阈值，应该过滤掉
        matches = store.query_cache(
            "Rust 系统编程性能优化", project="test", min_similarity=0.99
        )
        assert len(matches) == 0


class TestKnowledgeCollection:
    """知识库 collection 测试。"""

    def test_sync_and_search(self, store):
        """同步知识库后能语义搜索。"""
        store.sync_knowledge(
            sqlite_id=1,
            topic="Python asyncio 并发模型",
            conclusion="asyncio 是 Python 标准异步框架，基于事件循环和协程",
            sources="Python官方文档",
        )

        results = store.search_knowledge("异步编程 Python")
        assert len(results) >= 1
        assert results[0]["sqlite_id"] == 1

    def test_delete_knowledge(self, store):
        """删除知识库条目。"""
        store.sync_knowledge(sqlite_id=42, topic="测试条目", conclusion="测试结论")
        assert store.delete_knowledge(42) is True

    def test_knowledge_upsert(self, store):
        """upsert 更新已有条目。"""
        store.sync_knowledge(sqlite_id=1, topic="原主题", conclusion="原结论")
        store.sync_knowledge(sqlite_id=1, topic="新主题", conclusion="新结论")

        results = store.search_knowledge("新主题")
        assert len(results) >= 1


class TestCleanup:
    """过期清理测试。"""

    def test_cleanup_expired_empty(self, store):
        """空 collection 清理不报错。"""
        assert store.cleanup_expired(ttl_days=90) == 0

    def test_cleanup_zero_ttl(self, store):
        """TTL=0 不执行清理。"""
        assert store.cleanup_expired(ttl_days=0) == 0
