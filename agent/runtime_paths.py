# -*- coding: utf-8 -*-
"""Certus 路径解析 —— 一切相对于项目根目录。"""

import os

AGENT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(AGENT_DIR)

CONFIG_PATH = os.path.join(PROJECT_ROOT, "config.json")
DATA_DIR = os.path.join(PROJECT_ROOT, "data")
WORKSPACE_DIR = os.path.join(DATA_DIR, "workspace")
MEMORY_DIR = os.path.join(DATA_DIR, "memory")
PROFILES_DIR = os.path.join(AGENT_DIR, "profiles")

# 向后兼容 WebAISearch 旧引用
SKILL_DIR = PROJECT_ROOT
SCRIPT_DIR = AGENT_DIR


def ensure_runtime_dirs():
    for path in (DATA_DIR, WORKSPACE_DIR, MEMORY_DIR, PROFILES_DIR):
        os.makedirs(path, exist_ok=True)


def legacy_config_path():
    return CONFIG_PATH


def resolve_config_path():
    ensure_runtime_dirs()
    return CONFIG_PATH
