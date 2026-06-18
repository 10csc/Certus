# -*- coding: utf-8 -*-
"""Kimi 平台交互模块 —— React contenteditable 需要特殊键盘处理。"""

import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from playwright.sync_api import Page
from _base import find_input_box, find_submit_button, is_inside_message

CAPABILITIES = ["text_input", "file_upload"]
FILL_SEL = '[contenteditable="true"]'
EXTRACT_SEL = 'div[class*="chat-content-item-assistant"]'
VERIFY_BY_INPUT_CLEAR = False  # contenteditable 发送后不清空
SUBMIT_SELECTORS = []  # Kimi 只用 Enter


def fill_prompt(page, prompt_text):
    """填入提示词 —— Kimi React contenteditable 用 Home+Shift+End 全选。"""
    # 使用 _base 的通用查找（已过滤消息容器内的编辑区）
    el, _ = find_input_box(page, ['[contenteditable="true"]', 'textarea'])
    if not el:
        print("[kimi] 未找到输入框，尝试 Tab 导航", __import__("sys").stderr)
        try:
            page.keyboard.press("Tab")
            page.wait_for_timeout(500)
            page.keyboard.insert_text(prompt_text)
            page.wait_for_timeout(1000)
            val = page.evaluate(
                "document.activeElement ? "
                "(document.activeElement.innerText || document.activeElement.textContent || '') : ''")
            return len(val) > 5
        except Exception:
            return False

    try:
        el.click(timeout=3000)
        page.wait_for_timeout(300)
    except Exception:
        pass

    # Kimi React contenteditable: Ctrl+A 无效，用 Home+Shift+End 选中全部
    page.keyboard.press("Control+Home")
    page.wait_for_timeout(100)
    page.keyboard.press("Control+Shift+End")
    page.wait_for_timeout(100)
    page.keyboard.press("Backspace")
    page.wait_for_timeout(200)
    page.keyboard.insert_text(prompt_text)
    page.wait_for_timeout(500)
    return True


def submit(page):
    """Kimi 只用 Enter 发送。"""
    page.keyboard.press("Enter")
    page.wait_for_timeout(1500)
    return True


def dismiss_blockers(page):
    """Kimi: 不按 Escape（React contenteditable 上 Escape 会取消输入清除焦点）。"""
    try:
        page.on("dialog", lambda d: d.accept())
    except Exception:
        pass


def upload_file(page, file_path):
    """Kimi 文件上传：先点 toolkit 按钮，再找 file input。"""
    try:
        for sel in [".toolkit-trigger-btn", "[class*=toolkit] button", "button[class*=toolkit]"]:
            try:
                btn = page.locator(sel).first
                if btn.count() > 0 and btn.is_visible(timeout=2000):
                    btn.click(timeout=3000)
                    page.wait_for_timeout(1000)
                    break
            except Exception:
                continue
        fi = page.locator('input[type="file"]').first
        if fi.count() > 0:
            fi.set_input_files(file_path)
            page.wait_for_timeout(3000)
            return True
    except Exception as e:
        print(f"  [Kimi] 上传文件失败: {e}")
    return False
