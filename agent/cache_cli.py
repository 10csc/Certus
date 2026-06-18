# -*- coding: utf-8 -*-
"""缓存系统 CLI 入口 —— 供 C++ 端按需调用。

用法：
    python cache_cli.py search "查询文本" [--top_k 10] [--project xxx]
    python cache_cli.py knowledge_search "查询文本" [--top_k 10]
    python cache_cli.py knowledge_sync --action upsert --sqlite_id 1 --topic "主题" --conclusion "结论"
    python cache_cli.py knowledge_sync --action delete --sqlite_id 1
    python cache_cli.py stats
    python cache_cli.py cleanup [--ttl_days 90]

输出：JSON 结果到 stdout，日志到 stderr。
"""

import sys
import os
import json
import argparse

# 确保能导入 cache 模块
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from cache import get_store


def main():
    parser = argparse.ArgumentParser(description="Certus 缓存系统 CLI")
    subparsers = parser.add_subparsers(dest="command")

    # search: 搜索报告缓存
    p_search = subparsers.add_parser("search", help="语义搜索历史报告")
    p_search.add_argument("query", help="搜索问题")
    p_search.add_argument("--top_k", type=int, default=5)
    p_search.add_argument("--project", default="")
    p_search.add_argument("--min_similarity", type=float, default=0.85)

    # knowledge_search: 语义搜索知识库
    p_ks = subparsers.add_parser("knowledge_search", help="语义搜索知识库")
    p_ks.add_argument("query", help="搜索关键词")
    p_ks.add_argument("--top_k", type=int, default=10)

    # knowledge_sync: 同步知识库条目
    p_ksync = subparsers.add_parser("knowledge_sync", help="同步知识库到 ChromaDB")
    p_ksync.add_argument("--action", required=True, choices=["upsert", "delete"])
    p_ksync.add_argument("--sqlite_id", type=int, required=True)
    p_ksync.add_argument("--topic", default="")
    p_ksync.add_argument("--conclusion", default="")
    p_ksync.add_argument("--sources", default="")
    p_ksync.add_argument("--created_at", default="")

    # stats: 缓存统计
    subparsers.add_parser("stats", help="缓存统计信息")

    # cleanup: 清理过期缓存
    p_clean = subparsers.add_parser("cleanup", help="清理过期缓存")
    p_clean.add_argument("--ttl_days", type=int, default=90)

    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    store = get_store()
    if store is None:
        print(json.dumps({"error": "ChromaDB 初始化失败"}, ensure_ascii=False))
        sys.exit(1)

    try:
        if args.command == "search":
            matches = store.query_cache(
                args.query, project=args.project,
                top_k=args.top_k, min_similarity=args.min_similarity
            )
            print(json.dumps({"matches": matches}, ensure_ascii=False))

        elif args.command == "knowledge_search":
            results = store.search_knowledge(args.query, top_k=args.top_k)
            print(json.dumps({"results": results}, ensure_ascii=False))

        elif args.command == "knowledge_sync":
            if args.action == "upsert":
                store.sync_knowledge(
                    sqlite_id=args.sqlite_id,
                    topic=args.topic,
                    conclusion=args.conclusion,
                    sources=args.sources,
                    created_at=args.created_at,
                )
                print(json.dumps({"status": "ok"}, ensure_ascii=False))
            elif args.action == "delete":
                store.delete_knowledge(args.sqlite_id)
                print(json.dumps({"status": "ok"}, ensure_ascii=False))

        elif args.command == "stats":
            print(json.dumps(store.stats(), ensure_ascii=False))

        elif args.command == "cleanup":
            deleted = store.cleanup_expired(ttl_days=args.ttl_days)
            print(json.dumps({"deleted": deleted}, ensure_ascii=False))

    except Exception as e:
        print(json.dumps({"error": str(e)}, ensure_ascii=False))
        sys.exit(1)


if __name__ == "__main__":
    main()
