# -*- coding: utf-8 -*-
"""缓存协议处理器 —— 处理 C++ 端发来的缓存相关指令帧。

支持的指令：
- cache_query: 搜索前缓存查询
- cache_store: 搜索后存入报告
- knowledge_search: MemoryPage 语义搜索
"""

import sys
import os

from .store import CacheStore


class CacheProtocolHandler:
    """处理缓存相关的协议指令。"""

    def __init__(self, store):
        """
        Args:
            store: CacheStore 实例
        """
        self._store = store

    def handle(self, frame, write_frame_fn):
        """尝试处理一个指令帧。

        Args:
            frame: 从 stdin 读取的 JSON dict
            write_frame_fn: 写入帧的函数（来自 agent_protocol.write_frame）

        Returns:
            True 如果已处理，False 如果不是缓存指令
        """
        action = frame.get("action", "")

        if action == "cache_query":
            self._handle_query(frame, write_frame_fn)
            return True
        elif action == "cache_store":
            self._handle_store(frame, write_frame_fn)
            return True
        elif action == "knowledge_search":
            self._handle_knowledge_search(frame, write_frame_fn)
            return True

        return False

    def _handle_query(self, frame, write_fn):
        """处理 cache_query：语义搜索历史报告。"""
        query = frame.get("query", "")
        top_k = frame.get("top_k", 5)
        min_sim = frame.get("min_similarity", 0.85)

        if not query:
            write_fn("cache_miss", query="")
            return

        try:
            matches = self._store.query_cache(
                query, top_k=top_k, min_similarity=min_sim
            )
            if matches:
                write_fn("cache_hit", matches=matches)
            else:
                write_fn("cache_miss", query=query)
        except Exception as e:
            print(f"[Cache] 缓存查询失败: {e}", file=sys.stderr)
            write_fn("cache_miss", query=query)

    def _handle_store(self, frame, write_fn):
        """处理 cache_store：将报告存入缓存。"""
        query = frame.get("query", "")
        report_path = frame.get("report_path", "")

        if not query or not report_path:
            print("[Cache] cache_store 缺少必要参数", file=sys.stderr)
            return

        # 读取报告文件
        try:
            with open(report_path, "r", encoding="utf-8") as f:
                report_text = f.read()
        except Exception as e:
            print(f"[Cache] 读取报告文件失败: {e}", file=sys.stderr)
            return

        metadata = {
            "project": frame.get("project", ""),
            "platform": frame.get("platform", ""),
            "depth": frame.get("depth", "L2"),
            "report_path": report_path,
            "content_len": len(report_text),
            "reliability_confirmed": frame.get("reliability_confirmed", 0),
            "elapsed_sec": frame.get("elapsed_sec", 0),
            "sqlite_knowledge_id": frame.get("sqlite_knowledge_id", 0),
            "search_history_id": frame.get("search_history_id", 0),
        }

        try:
            doc_id = self._store.store_report(query, report_text, metadata)
            print(f"[Cache] 报告已存入: {doc_id}", file=sys.stderr)
            write_fn("cache_stored", id=doc_id, query=query)
        except Exception as e:
            print(f"[Cache] 存入失败: {e}", file=sys.stderr)

    def _handle_knowledge_search(self, frame, write_fn):
        """处理 knowledge_search：语义搜索知识库。"""
        query = frame.get("query", "")
        top_k = frame.get("top_k", 10)

        if not query:
            write_fn("knowledge_search_results", results=[])
            return

        try:
            results = self._store.search_knowledge(query, top_k=top_k)
            write_fn("knowledge_search_results", results=results)
        except Exception as e:
            print(f"[Cache] 知识库搜索失败: {e}", file=sys.stderr)
            write_fn("knowledge_search_results", results=[])

    # ============================================================
    # 知识库同步（供 C++ 端 CRUD 操作时调用）
    # ============================================================

    def handle_knowledge_sync(self, frame, write_fn):
        """处理知识库同步指令（knowledge_sync）。

        C++ 端增删改知识库时发送此指令，同步到 ChromaDB。
        """
        action = frame.get("sync_action", "")
        sqlite_id = frame.get("sqlite_id", 0)

        if action == "upsert":
            self._store.sync_knowledge(
                sqlite_id=sqlite_id,
                topic=frame.get("topic", ""),
                conclusion=frame.get("conclusion", ""),
                sources=frame.get("sources", ""),
                created_at=frame.get("created_at", ""),
            )
        elif action == "delete":
            self._store.delete_knowledge(sqlite_id)

        return True
