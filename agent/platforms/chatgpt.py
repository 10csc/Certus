# -*- coding: utf-8 -*-
"""ChatGPT ProseMirror"""
from playwright.sync_api import Page
import time

CAPABILITIES = ["text_input"]
FILL_SEL = "#prompt-textarea, textarea"
EXTRACT_SEL = 'div[class*="markdown"]'

def fill_prompt(page, prompt_text):
    page.locator("body").click(timeout=3000)
    time.sleep(1)
    sel = "#prompt-textarea, .ProseMirror[contenteditable=true]"
    page.locator(sel).first.wait_for(state="visible", timeout=10000)
    page.locator(sel).first.click(timeout=5000)
    time.sleep(0.5)
    page.keyboard.press("Control+a")
    page.keyboard.press("Backspace")
    time.sleep(0.2)
    page.keyboard.insert_text(prompt_text)
    time.sleep(1)
    val = page.evaluate("""() => {
        let e = document.querySelector("#prompt-textarea");
        return e ? (e.innerText || e.textContent || "") : "";
    }""")
    return len(val) > 10

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
    sel = "button[data-testid=send-button], button[type=submit], [aria-label=Send], button svg"
    try:
        page.locator(sel).first.click(timeout=5000)
        time.sleep(2)
        return True
    except Exception:
        pass
    try:
        page.keyboard.press("Enter")
        time.sleep(2)
        return True
    except Exception:
        return False
