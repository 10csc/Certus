# -*- coding: utf-8 -*-
"""平台定型 Agent —— 分析平台 DOM，生成/更新交互脚本。

用法: python typing_agent.py <platform> <url> [--cdp-port PORT]
示例: python typing_agent.py chatgpt https://chatgpt.com/ --cdp-port 9223

流程:
  1. 通过 CDP 连接已有浏览器
  2. 打开平台 URL
  3. 提取页面 DOM 关键元素
  4. 调用 LLM 生成交互脚本
  5. 保存到 agent/platforms/<platform>.py
"""
import sys, os, json, time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)


def run_typing(platform, url, cdp_port=9223):
    """执行平台定型。返回 (success, message)。"""
    print(f"[Typing] 开始定型: platform={platform}, url={url}, cdp={cdp_port}")

    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        return False, "Playwright 未安装"

    try:
        with sync_playwright() as p:
            # 连接到已有 CDP 浏览器
            browser = p.chromium.connect_over_cdp(
                f"http://127.0.0.1:{cdp_port}")
            if not browser.contexts:
                return False, "浏览器无活跃上下文"
            context = browser.contexts[0]

            # 打开目标页面
            print(f"[Typing] 打开 {url} ...")
            page = context.new_page()
            page.goto(url, timeout=30000, wait_until="domcontentloaded")
            time.sleep(3)

            # 分析 DOM
            print(f"[Typing] 分析 DOM...")
            dom_info = page.evaluate("""() => {
                let info = {url: location.href, title: document.title,
                            inputs: [], buttons: [], textareas: []};

                // 找所有可编辑区域
                let editables = document.querySelectorAll(
                    '[contenteditable="true"], textarea, input[type="text"], [role="textbox"]');
                editables.forEach((el, i) => {
                    if (i > 5) return;
                    let parent = el.parentElement;
                    info.inputs.push({
                        tag: el.tagName,
                        id: el.id || '',
                        class: (el.className || '').substring(0, 80),
                        placeholder: el.getAttribute('placeholder') || el.getAttribute('data-placeholder') || '',
                        role: el.getAttribute('role') || '',
                        aria_label: el.getAttribute('aria-label') || '',
                        in_form: !!el.closest('form'),
                        ancestor_classes: (parent ? (parent.className || '').substring(0, 80) : ''),
                    });
                });

                // 找所有按钮
                let buttons = document.querySelectorAll('button, [role="button"]');
                buttons.forEach((btn, i) => {
                    if (i > 10) return;
                    info.buttons.push({
                        text: (btn.innerText || '').substring(0, 30),
                        aria_label: btn.getAttribute('aria-label') || '',
                        data_testid: btn.getAttribute('data-testid') || '',
                        id: btn.id || '',
                        class: (btn.className || '').substring(0, 60),
                        type: btn.getAttribute('type') || '',
                        has_svg: !!btn.querySelector('svg'),
                    });
                });

                // 找所有 textarea
                let tas = document.querySelectorAll('textarea');
                tas.forEach((ta, i) => {
                    if (i > 3) return;
                    info.textareas.push({
                        id: ta.id || '',
                        name: ta.getAttribute('name') || '',
                        placeholder: ta.getAttribute('placeholder') || '',
                    });
                });

                return info;
            }""")

            print(f"[Typing] DOM 分析完成: {len(dom_info.get('inputs',[]))} 输入区, "
                  f"{len(dom_info.get('buttons',[]))} 按钮")

            # 调用 LLM 生成脚本
            from generator import generate_interaction_script
            script = generate_interaction_script(page, platform, url)
            if script:
                print(f"[Typing] 脚本已生成并保存")
                return True, f"定型完成: agent/platforms/{platform}.py"
            else:
                return False, "LLM 生成失败"

    except Exception as e:
        import traceback
        traceback.print_exc()
        return False, str(e)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Certus 平台定型工具")
    parser.add_argument("platform", help="平台名 (chatgpt/deepseek/kimi/gemini)")
    parser.add_argument("url", help="平台 URL")
    parser.add_argument("--cdp-port", type=int, default=9223, help="CDP 端口")
    args = parser.parse_args()

    ok, msg = run_typing(args.platform, args.url, args.cdp_port)
    print(f"\n{'✓' if ok else '✗'} {msg}")
    sys.exit(0 if ok else 1)
