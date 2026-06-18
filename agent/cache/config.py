# -*- coding: utf-8 -*-
"""缓存系统配置常量。"""

# 语义相似度命中阈值（cosine similarity）
MIN_SIMILARITY = 0.85

# 默认返回匹配条数
DEFAULT_TOP_K = 5

# 缓存过期天数（0 = 永不过期）
CACHE_TTL_DAYS = 90

# ChromaDB collection 名称
COLLECTION_REPORTS = "certus_reports"
COLLECTION_KNOWLEDGE = "certus_knowledge"

# 报告摘要最大长度（去 Markdown 后截取）
SUMMARY_MAX_LEN = 2000

# ChromaDB 持久化子目录名（相对于 MEMORY_DIR）
CHROMA_DIR_NAME = "chroma"
