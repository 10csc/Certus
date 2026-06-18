# -*- coding: utf-8 -*-
"""ChromaDB 缓存存储引擎 —— 搜索报告的语义缓存层。

核心功能：
- query_cache(): 语义搜索历史报告（cosine similarity）
- store_report(): 将完成搜索的报告存入缓存
- delete_by_id(): 删除指定缓存条目
- stats(): 缓存统计信息
"""

import re
import sys
import time
import hashlib
from datetime import datetime, timezone, timedelta

from .config import (
    COLLECTION_REPORTS,
    COLLECTION_KNOWLEDGE,
    SUMMARY_MAX_LEN,
    MIN_SIMILARITY,
    CACHE_TTL_DAYS,
)
from .embedder import CertusEmbedder

CST = timezone(timedelta(hours=8))


class CacheStore:
    """ChromaDB 缓存存储引擎。"""

    def __init__(self, persist_dir, embedding_function=None):
        """初始化 ChromaDB 持久化客户端。

        Args:
            persist_dir: ChromaDB 数据目录（通常是 data/memory/chroma/）
            embedding_function: 可选嵌入函数，None 则使用默认
        """
        import chromadb

        self._client = chromadb.PersistentClient(path=persist_dir)

        # 嵌入函数
        self._embedder = embedding_function or CertusEmbedder()

        # 搜索报告 collection
        self._reports = self._client.get_or_create_collection(
            name=COLLECTION_REPORTS,
            metadata={"hnsw:space": "cosine"},
            embedding_function=self._embedder,
        )

        # 知识库 collection（延迟创建，阶段 4 使用）
        self._knowledge = None

    # ============================================================
    # 搜索报告缓存
    # ============================================================

    def query_cache(self, query, project="", top_k=5,
                    min_similarity=MIN_SIMILARITY):
        """语义搜索历史报告。返回高相似度匹配列表。

        Args:
            query: 搜索问题
            project: 项目名过滤（空则不过滤）
            top_k: 返回最大条数
            min_similarity: 最低相似度阈值

        Returns:
            list[dict]: 匹配列表，每项含 query/similarity/report_path/created_at 等
        """
        where_filter = None
        if project:
            where_filter = {"project": project}

        results = self._reports.query(
            query_texts=[query],
            n_results=top_k,
            where=where_filter,
            include=["documents", "metadatas", "distances"],
        )

        matches = []
        if not results or not results["distances"] or not results["distances"][0]:
            return matches

        for i, dist in enumerate(results["distances"][0]):
            similarity = 1.0 - dist  # cosine distance → similarity
            if similarity >= min_similarity:
                meta = results["metadatas"][0][i]
                matches.append({
                    "query": meta.get("query", ""),
                    "similarity": round(similarity, 3),
                    "report_path": meta.get("report_path", ""),
                    "created_at": meta.get("created_at", ""),
                    "project": meta.get("project", ""),
                    "content_len": meta.get("content_len", 0),
                    "platform": meta.get("platform", ""),
                    "elapsed_sec": meta.get("elapsed_sec", 0),
                })
        return matches

    def store_report(self, query, report_text, metadata):
        """将报告存入缓存。

        Args:
            query: 原始搜索问题
            report_text: 报告完整文本
            metadata: 附加元数据 dict

        Returns:
            str: 文档 ID
        """
        doc_id = self._make_doc_id(query)
        summary = self._summarize(report_text, SUMMARY_MAX_LEN)

        now_str = datetime.now(CST).isoformat()

        self._reports.add(
            ids=[doc_id],
            documents=[summary],
            metadatas=[{
                "query": query,
                "project": metadata.get("project", ""),
                "platform": metadata.get("platform", ""),
                "depth": metadata.get("depth", "L2"),
                "report_path": metadata.get("report_path", ""),
                "content_len": int(metadata.get("content_len", 0)),
                "reliability_confirmed": int(metadata.get("reliability_confirmed", 0)),
                "elapsed_sec": float(metadata.get("elapsed_sec", 0)),
                "created_at": now_str,
                "sqlite_knowledge_id": int(metadata.get("sqlite_knowledge_id", 0)),
                "search_history_id": int(metadata.get("search_history_id", 0)),
            }],
        )
        return doc_id

    def delete_by_id(self, doc_id):
        """删除指定缓存条目。"""
        try:
            self._reports.delete(ids=[doc_id])
            return True
        except Exception as e:
            print(f"[Cache] 删除失败: {e}", file=sys.stderr)
            return False

    # ============================================================
    # 知识库语义索引（阶段 4）
    # ============================================================

    def _ensure_knowledge_collection(self):
        if self._knowledge is None:
            self._knowledge = self._client.get_or_create_collection(
                name=COLLECTION_KNOWLEDGE,
                metadata={"hnsw:space": "cosine"},
                embedding_function=self._embedder,
            )

    def sync_knowledge(self, sqlite_id, topic, conclusion, sources="",
                       created_at=""):
        """将一条知识库条目同步到 ChromaDB。

        Args:
            sqlite_id: SQLite knowledge 表的 id
            topic: 主题
            conclusion: 结论
            sources: 来源
            created_at: 创建时间
        """
        self._ensure_knowledge_collection()
        doc_id = f"knowledge_{sqlite_id}"
        doc_text = f"{topic}。{conclusion}"

        self._knowledge.upsert(
            ids=[doc_id],
            documents=[doc_text],
            metadatas=[{
                "sqlite_id": int(sqlite_id),
                "topic": topic,
                "sources": sources,
                "created_at": created_at,
            }],
        )

    def search_knowledge(self, query, top_k=10):
        """语义搜索知识库。

        Returns:
            list[dict]: 匹配列表，每项含 topic/conclusion/similarity
        """
        self._ensure_knowledge_collection()
        if self._knowledge.count() == 0:
            return []

        results = self._knowledge.query(
            query_texts=[query],
            n_results=top_k,
            include=["metadatas", "distances"],
        )

        matches = []
        if not results or not results["distances"]:
            return matches

        for i, dist in enumerate(results["distances"][0]):
            similarity = 1.0 - dist
            meta = results["metadatas"][0][i]
            matches.append({
                "sqlite_id": meta.get("sqlite_id", 0),
                "topic": meta.get("topic", ""),
                "sources": meta.get("sources", ""),
                "similarity": round(similarity, 3),
                "created_at": meta.get("created_at", ""),
            })
        return matches

    def delete_knowledge(self, sqlite_id):
        """从 ChromaDB 删除一条知识库条目。"""
        self._ensure_knowledge_collection()
        try:
            self._knowledge.delete(ids=[f"knowledge_{sqlite_id}"])
            return True
        except Exception:
            return False

    # ============================================================
    # 统计与维护
    # ============================================================

    def stats(self):
        """返回缓存统计信息。"""
        result = {
            "total_reports": self._reports.count(),
        }
        if self._knowledge is not None:
            result["total_knowledge"] = self._knowledge.count()
        return result

    def cleanup_expired(self, ttl_days=CACHE_TTL_DAYS):
        """清理过期缓存条目。

        Returns:
            int: 删除的条目数
        """
        if ttl_days <= 0:
            return 0

        cutoff = datetime.now(CST) - timedelta(days=ttl_days)
        cutoff_str = cutoff.isoformat()
        deleted = 0

        try:
            all_data = self._reports.get(include=["metadatas"])
            if not all_data or not all_data["ids"]:
                return 0

            expired_ids = []
            for i, meta in enumerate(all_data["metadatas"]):
                created = meta.get("created_at", "")
                if created and created < cutoff_str:
                    expired_ids.append(all_data["ids"][i])

            if expired_ids:
                self._reports.delete(ids=expired_ids)
                deleted = len(expired_ids)
                print(f"[Cache] 清理过期缓存: {deleted} 条", file=sys.stderr)
        except Exception as e:
            print(f"[Cache] 清理过期缓存失败: {e}", file=sys.stderr)

        return deleted

    # ============================================================
    # 内部工具
    # ============================================================

    @staticmethod
    def _make_doc_id(query):
        """生成文档 ID：report_{timestamp}_{hash}。"""
        ts = int(time.time())
        h = hashlib.md5(query.encode("utf-8")).hexdigest()[:8]
        return f"report_{ts}_{h}"

    @staticmethod
    def _summarize(text, max_len=SUMMARY_MAX_LEN):
        """提取纯文本摘要（去 Markdown 格式标记）。"""
        clean = re.sub(r'[#*`~>\[\]()!|]', '', text)
        clean = re.sub(r'\n{2,}', '\n', clean).strip()
        return clean[:max_len]
