# -*- coding: utf-8 -*-
"""DeepSeek 平台交互模块。"""

import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from playwright.sync_api import Page
from _base import safe_fill, safe_submit, dismiss_blockers_base

CAPABILITIES = ["text_input", "file_upload"]
FILL_SEL = '[contenteditable="true"], textarea, [role="textbox"]'
EXTRACT_SEL = 'div[class*="assistant"], div[data-role="assistant"], div[class*="message"][class*="ai"]'

FILL_SELECTORS = ['[contenteditable="true"]', 'textarea', '[role="textbox"]']
SUBMIT_SELECTORS = [
    'button[aria-label=发送]', 'button[aria-label=Send]',
    '[aria-label=Send message]', 'button[data-testid=send-button]',
]


def fill_prompt(page, prompt_text):
    return safe_fill(page, prompt_text, FILL_SELECTORS)


def submit(page):
    return safe_submit(page, SUBMIT_SELECTORS)


def dismiss_blockers(page):
    dismiss_blockers_base(page)


def upload_file(page, file_path):
    try:
        file_input = page.locator('input[type="file"]').first
        file_input.wait_for(state="attached", timeout=5000)
        file_input.set_input_files(file_path)
        page.wait_for_timeout(2000)
        return True
    except Exception as e:
        print(f"  [DeepSeek] 上传文件失败: {e}")
        return False
