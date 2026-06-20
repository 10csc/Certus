# -*- coding: utf-8 -*-
"""平台交互基类 —— 通用输入框/发送按钮查找逻辑。

所有平台脚本共享此模块的查找策略，避免每个平台重复造轮子。
加新平台只需覆盖选择器差异化部分，核心查找逻辑在此统一维护。
"""
from playwright.sync_api import Page
import time


def is_inside_message(el_handle):
    """检查元素是否在 AI 回复/消息容器内（应跳过）。"""
    try:
        inside = el_handle.evaluate("""(el) => {
            let p = el.parentElement;
            for (let i = 0; i < 12 && p; i++) {
                let cls = (p.className || '').toString();
                let role = (p.getAttribute('role') || '').toString();
                let tag = p.tagName.toLowerCase();
                if (cls.includes('message') || cls.includes('response') ||
                    cls.includes('conversation') || cls.includes('turn') ||
                    cls.includes('chat-turn') || cls.includes('agent-turn') ||
                    cls.includes('model-turn') || cls.includes('assistant') ||
                    role === 'article' || role === 'listitem' ||
                    (tag === 'article') || (tag === 'li'))
                    return true;
                p = p.parentElement;
            }
            return false;
        }""")
        return bool(inside)
    except Exception:
        return True  # 不确定时保守跳过


def find_input_box(page, custom_selectors=None, prefer_last=True):
    """通用的输入框查找。

    策略（按优先级）：
      1. 自定义精确选择器（平台特定）
      2. 所有 [contenteditable=true]，从后往前找第一个不在消息内的
      3. 所有 textarea，同上
      4. 带 data-placeholder 的 p 元素
      5. Tab 键导航

    prefer_last=True: 输入框通常在页面底部，从后往前找
    """
    # 策略 1: 平台特定选择器
    if custom_selectors:
        for sel in custom_selectors:
            try:
                el = page.locator(sel).first
                if el.is_visible():
                    return el, sel
            except Exception:
                continue

    # 策略 2: contenteditable
    try:
        all_editable = page.locator('div[contenteditable="true"]')
        count = all_editable.count()
        rng = range(count - 1, -1, -1) if prefer_last else range(count)
        for i in rng:
            el = all_editable.nth(i)
            if not el.is_visible():
                continue
            try:
                handle = el.element_handle()
                if handle and not is_inside_message(handle):
                    return el, 'div[contenteditable="true"](filtered)'
            except Exception:
                continue
    except Exception:
        pass

    # 策略 3: textarea
    try:
        all_ta = page.locator('textarea')
        count = all_ta.count()
        rng = range(count - 1, -1, -1) if prefer_last else range(count)
        for i in rng:
            el = all_ta.nth(i)
            if not el.is_visible():
                continue
            try:
                handle = el.element_handle()
                if handle and not is_inside_message(handle):
                    return el, 'textarea(filtered)'
            except Exception:
                continue
    except Exception:
        pass

    # 策略 4: data-placeholder
    try:
        el = page.locator('p[data-placeholder]').last
        if el.is_visible():
            return el, 'p[data-placeholder]'
    except Exception:
        pass

    return None, None


def find_submit_button(page, custom_selectors=None):
    """通用的发送按钮查找。

    策略：
      1. 自定义精确选择器（平台特定）
      2. 通用选择器，但跳过消息容器内的
      3. form 内带 svg 图标的按钮
      4. Enter 键兜底（注意 ProseMirror 中 Enter 可能只是换行）
    """
    # 策略 1: 平台特定
    if custom_selectors:
        for sel in custom_selectors:
            try:
                btn = page.locator(sel).first
                if btn.is_visible():
                    return btn, sel
            except Exception:
                continue

    # 策略 2: 通用选择器，排除消息容器
    generic = [
        'button[aria-label*="Send"]',
        'button[aria-label*="发送"]',
        'button[data-testid="send-button"]',
        'button[type="submit"]:not([disabled])',
    ]
    for sel in generic:
        try:
            candidates = page.locator(sel)
            for i in range(min(candidates.count(), 10)):
                btn = candidates.nth(i)
                if not btn.is_visible():
                    continue
                try:
                    handle = btn.element_handle()
                    if handle and not is_inside_message(handle):
                        return btn, sel
                except Exception:
                    continue
        except Exception:
            continue

    # 策略 3: form 内的 svg 按钮
    try:
        btn = page.locator('form button:has(svg)').first
        if btn.is_visible():
            return btn, 'form button:has(svg)'
    except Exception:
        pass

    return None, None


def safe_fill(page, text, custom_selectors=None):
    """通用的安全填入提示词流程。

    1. 激活页面 → 2. 找输入框 → 3. 清空 → 4. 填入 → 5. 验证
    """
    # 激活
    try:
        page.locator("body").click(timeout=2000)
    except Exception:
        pass
    time.sleep(0.3)

    el, how = find_input_box(page, custom_selectors)
    if not el:
        print(f"[base] 未找到输入框，尝试 Tab 导航")
        try:
            page.keyboard.press("Tab")
            time.sleep(0.5)
            page.keyboard.insert_text(text)
            time.sleep(1)
            val = page.evaluate(
                "document.activeElement ? "
                "(document.activeElement.innerText || "
                "document.activeElement.textContent || '') : ''")
            return len(val) > 5
        except Exception:
            return False

    try:
        el.click(timeout=3000)
        time.sleep(0.3)
    except Exception:
        pass

    # 清空 + 填入
    try:
        page.keyboard.press("Control+a")
        page.keyboard.press("Backspace")
        time.sleep(0.2)
    except Exception:
        pass

    page.keyboard.insert_text(text)
    time.sleep(1)
    return True


def safe_submit(page, custom_selectors=None):
    """通用的安全提交流程。

    1. 找发送按钮 → 2. 点击 → 3. Enter 键兜底
    """
    btn, how = find_submit_button(page, custom_selectors)
    if btn:
        try:
            btn.click(timeout=3000)
            time.sleep(2)
            return True
        except Exception:
            pass

    # Enter 兜底
    try:
        page.keyboard.press("Enter")
        time.sleep(2)
        return True
    except Exception:
        return False


def dismiss_blockers_base(page):
    """通用弹窗消除。"""
    try:
        page.on("dialog", lambda d: d.accept())
    except Exception:
        pass
    try:
        page.keyboard.press("Escape")
        time.sleep(0.3)
    except Exception:
        pass


def find_file_input(page):
    """查找文件上传入口。

    策略：
      1. input[type="file"]（隐藏或可见）
      2. 带上传相关 aria-label/text 的按钮
      3. 带 upload/attach/file 类名的按钮
    返回 (element, method) 或 (None, None)。
    """
    # 策略 1: input[type="file"]（通常隐藏，但 Playwright 可直接 set_input_files）
    try:
        file_inputs = page.locator('input[type="file"]')
        count = file_inputs.count()
        if count > 0:
            return file_inputs.first, "input_file"
    except Exception:
        pass

    # 策略 2: 带上传语义的按钮
    upload_selectors = [
        'button[aria-label*="upload" i]',
        'button[aria-label*="Upload"]',
        'button[aria-label*="上传"]',
        'button[aria-label*="附件"]',
        'button[aria-label*="attach" i]',
        'button[data-testid*="upload"]',
        'button[data-testid*="attach"]',
        '[role="button"][aria-label*="upload" i]',
    ]
    for sel in upload_selectors:
        try:
            btn = page.locator(sel).first
            if btn.is_visible():
                return btn, "upload_button"
        except Exception:
            continue

    # 策略 3: 带上传相关文字的按钮/图标
    try:
        result = page.evaluate("""() => {
            let els = document.querySelectorAll('button, [role=button], svg');
            for (let el of els) {
                let a = (el.getAttribute('aria-label') || '').toLowerCase();
                let t = (el.textContent || '').toLowerCase();
                let c = (el.className || '').toString().toLowerCase();
                if (a.includes('upload') || a.includes('file') || a.includes('attach') ||
                    a.includes('上传') || a.includes('附件') || a.includes('文件') ||
                    t.includes('上传') || t.includes('附件') ||
                    c.includes('upload') || c.includes('attach')) {
                    let r = el.getBoundingClientRect();
                    if (r.width > 5 && r.height > 5) {
                        return {
                            tag: el.tagName,
                            aria: a.substring(0, 60),
                            text: t.substring(0, 40),
                            className: c.substring(0, 80),
                            rect: {x: Math.round(r.x), y: Math.round(r.y),
                                   w: Math.round(r.width), h: Math.round(r.height)},
                        };
                    }
                }
            }
            return null;
        }""")
        if result:
            return result, "upload_heuristic"
    except Exception:
        pass

    return None, None


def safe_upload_file(page, file_path, custom_selectors=None):
    """通用文件上传流程。

    1. 查找 input[type="file"] → 直接 set_input_files（最可靠）
    2. 查找上传按钮 → 点击触发文件选择器 → 通过 file_chooser 事件设置
    3. 都失败 → 返回 False
    """
    import os
    if not os.path.exists(file_path):
        print(f"[base] 文件不存在: {file_path}")
        return False

    # 策略 1: input[type="file"] 直接设置（不需要点击）
    try:
        file_input = page.locator('input[type="file"]').first
        file_input.set_input_files(file_path)
        time.sleep(1)
        print(f"[base] ✓ input[type=file] 直接设置成功")
        return True
    except Exception as e:
        print(f"[base] input[type=file] 不可用: {e}")

    # 策略 2: 点击上传按钮 + file_chooser 事件
    try:
        upload_btn = None
        if custom_selectors:
            for sel in custom_selectors:
                try:
                    btn = page.locator(sel).first
                    if btn.is_visible():
                        upload_btn = btn
                        break
                except Exception:
                    continue

        if not upload_btn:
            el, method = find_file_input(page)
            if el and method == "upload_button":
                upload_btn = el

        if upload_btn:
            with page.expect_file_chooser(timeout=5000) as fc_info:
                upload_btn.click()
            file_chooser = fc_info.value
            file_chooser.set_files(file_path)
            time.sleep(1)
            print(f"[base] ✓ 通过 file_chooser 上传成功")
            return True
    except Exception as e:
        print(f"[base] file_chooser 方式失败: {e}")

    return False
