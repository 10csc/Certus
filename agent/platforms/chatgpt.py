# -*- coding: utf-8 -*-
"""ChatGPT 平台交互脚本 —— 使用 _base.py 通用查找逻辑。

关键约束：GPT 的 AI 回复也可能是可编辑区域。
_base.py 的 find_input_box / find_submit_button 已内置消息容器过滤。
"""

import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from playwright.sync_api import Page
from _base import safe_fill, safe_submit, dismiss_blockers_base

CAPABILITIES = ["text_input"]
FILL_SEL = '#prompt-textarea, [data-placeholder]'
EXTRACT_SEL = 'div[class*="markdown"], article[data-testid*="conversation"]'

# ChatGPT 专用选择器（_base 的通用策略之前先尝试这些）
FILL_SELECTORS = [
    '#prompt-textarea',
    'div.ProseMirror#prompt-textarea',
    'form textarea',
]
SUBMIT_SELECTORS = [
    'button[aria-label="Send prompt"]',
    'button[data-testid="send-button"]',
]


def fill_prompt(page, prompt_text):
    return safe_fill(page, prompt_text, FILL_SELECTORS)


def submit(page):
    return safe_submit(page, SUBMIT_SELECTORS)


def dismiss_blockers(page):
    dismiss_blockers_base(page)
