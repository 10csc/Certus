# -*- coding: utf-8 -*-
"""{platform} 平台交互模块（兜底模板）"""
from playwright.sync_api import Page
import time, os

CAPABILITIES = ["text_input"]
EXTRACT_SEL = 'div[class*="message"]'

def fill_prompt(page, prompt_text):
    page.locator("[contenteditable=true], textarea, [role=textbox]").first.click(timeout=5000)
    time.sleep(0.5)
    page.keyboard.press("Control+a")
    page.keyboard.press("Backspace")
    page.keyboard.insert_text(prompt_text)
    time.sleep(0.5)
    return True

def dismiss_blockers(page):
    try:
        page.on("dialog", lambda d: d.accept())
    except Exception:
        pass
    try:
        page.keyboard.press("Escape")
        time.sleep(0.3)
    except Exception:
        pass

def submit(page):
    """优先点击发送按钮，兜底 Enter"""
    for sel in ["button[aria-label=发送]", "button[aria-label=Send]", "[aria-label=Send message]", "button[aria-label=Send message]", "button[data-testid=send-button]"]:
        try:
            btn = page.locator(sel).first
            if btn.is_visible(timeout=1000):
                btn.click(timeout=3000)
                time.sleep(1.5)
                return True
        except Exception:
            continue
    try:
        page.keyboard.press("Enter")
        time.sleep(1.5)
        return True
    except Exception:
        return False

def upload_file(page, file_path):
    """通用文件上传：优先 input[type=file]，其次上传按钮 + file_chooser"""
    if not os.path.exists(file_path):
        return False
    # 策略 1: input[type="file"] 直接设置
    try:
        fi = page.locator("input[type=file]").first
        fi.set_input_files(file_path)
        time.sleep(1)
        return True
    except Exception:
        pass
    # 策略 2: 上传按钮 + file_chooser
    try:
        for sel in ["button[aria-label*=upload]", "button[aria-label*=上传]", "button[aria-label*=attach]", "button[aria-label*=附件]"]:
            try:
                btn = page.locator(sel).first
                if btn.is_visible(timeout=1000):
                    with page.expect_file_chooser(timeout=5000) as fc:
                        btn.click()
                    fc.value.set_files(file_path)
                    time.sleep(1)
                    return True
            except Exception:
                continue
    except Exception:
        pass
    return False

