# -*- coding: utf-8 -*-
"""嵌入函数封装 —— 延迟导入，支持模型切换。

默认使用 ChromaDB 内置的 all-MiniLM-L6-v2（384 维）。
可通过 CertusEmbedder(model_name="BAAI/bge-small-zh-v1.5") 切换中文优化模型。
"""


class CertusEmbedder:
    """可切换的嵌入函数包装。"""

    def __init__(self, model_name=None):
        self._model_name = model_name
        self._fn = None

    def _ensure_init(self):
        if self._fn is not None:
            return
        from chromadb.utils.embedding_functions import DefaultEmbeddingFunction
        if self._model_name:
            from chromadb.utils.embedding_functions import (
                SentenceTransformerEmbeddingFunction,
            )
            self._fn = SentenceTransformerEmbeddingFunction(
                model_name=self._model_name
            )
        else:
            self._fn = DefaultEmbeddingFunction()

    def __call__(self, input):
        """兼容 ChromaDB embedding function 协议。"""
        self._ensure_init()
        return self._fn(input)

    def get_embedding_function(self):
        """返回 ChromaDB 可接受的嵌入函数对象。"""
        self._ensure_init()
        return self._fn
