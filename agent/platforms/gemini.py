# -*- coding: utf-8 -*-
"""Gemini 平台交互模块。"""

import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from playwright.sync_api import Page
from _base import safe_fill, safe_submit, dismiss_blockers_base

CAPABILITIES = ["text_input"]
FILL_SEL = '[contenteditable="true"], textarea, [role="textbox"]'
EXTRACT_SEL = 'div[class*="model-response"], div[class*="bot-message"], div[class*="ai-response"], div[class*="response-content"]'

FILL_SELECTORS = ['[contenteditable="true"]', 'textarea', '[role="textbox"]']
SUBMIT_SELECTORS = [
    'button[aria-label=发送]', 'button[aria-label=Send]',
    '[aria-label=Send message]', 'button[aria-label=Send message]',
    'button[data-testid=send-button]',
]


def fill_prompt(page, prompt_text):
    return safe_fill(page, prompt_text, FILL_SELECTORS)


def submit(page):
    return safe_submit(page, SUBMIT_SELECTORS)


def dismiss_blockers(page):
    dismiss_blockers_base(page)
