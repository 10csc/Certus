# -*- coding: utf-8 -*-
"""Certus 缓存系统 —— ChromaDB 语义搜索层。

初始化策略：
- 延迟导入：只在需要时 import chromadb，不影响 agent 启动时间
- 容错降级：ChromaDB 不可用时静默降级，不影响核心搜索流程
"""

import sys

_store_instance = None
_handler_instance = None


def get_store():
    """获取 CacheStore 单例。首次调用时初始化。

    Returns:
        CacheStore 实例，失败返回 None
    """
    global _store_instance
    if _store_instance is not None:
        return _store_instance

    try:
        from .store import CacheStore
        from .config import CHROMA_DIR_NAME

        # 从 runtime_paths 获取 MEMORY_DIR
        import os
        agent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        memory_dir = os.path.join(os.path.dirname(agent_dir), "data", "memory")
        chroma_dir = os.path.join(memory_dir, CHROMA_DIR_NAME)
        os.makedirs(chroma_dir, exist_ok=True)

        _store_instance = CacheStore(persist_dir=chroma_dir)
        print(f"[Cache] 初始化成功: {chroma_dir}", file=sys.stderr)
        return _store_instance
    except ImportError as e:
        print(f"[Cache] chromadb 未安装，缓存功能不可用: {e}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"[Cache] 初始化失败: {e}", file=sys.stderr)
        return None


def get_handler():
    """获取 CacheProtocolHandler 单例。

    Returns:
        CacheProtocolHandler 实例，失败返回 None
    """
    global _handler_instance
    if _handler_instance is not None:
        return _handler_instance

    store = get_store()
    if store is None:
        return None

    try:
        from .protocol_handler import CacheProtocolHandler
        _handler_instance = CacheProtocolHandler(store)
        return _handler_instance
    except Exception as e:
        print(f"[Cache] 创建 handler 失败: {e}", file=sys.stderr)
        return None


def try_handle(frame, write_frame_fn):
    """尝试用缓存系统处理一个指令帧。

    这是外部调用的统一入口。

    Args:
        frame: 从 stdin 读取的 JSON dict
        write_frame_fn: 写入帧的函数

    Returns:
        True 如果已处理，False 如果不是缓存指令或缓存不可用
    """
    action = frame.get("action", "")

    # 快速过滤：不是缓存指令直接返回 False
    cache_actions = {"cache_query", "cache_store", "knowledge_search", "knowledge_sync"}
    if action not in cache_actions:
        return False

    handler = get_handler()
    if handler is None:
        print(f"[Cache] 缓存系统不可用，忽略指令: {action}", file=sys.stderr)
        return False

    if action == "knowledge_sync":
        return handler.handle_knowledge_sync(frame, write_frame_fn)
    return handler.handle(frame, write_frame_fn)
