# -*- coding: utf-8 -*-
"""LLM 代码生成器：分析页面 DOM → 生成平台交互脚本 → 缓存"""
import json, os, time, hashlib, importlib.util
from common import SCRIPTS_DIR, load_config
from runtime_paths import PROFILES_DIR

PLATFORMS_DIR = os.path.join(SCRIPTS_DIR, "platforms")


def get_platform_script_path(platform):
    return os.path.join(PLATFORMS_DIR, f"{platform}.py")


def load_platform_module(platform):
    """加载平台脚本模块，所有调用方统一入口。"""
    spec = importlib.util.spec_from_file_location(
        f"platform_{platform}", get_platform_script_path(platform))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def get_profile_path(url):
    h = hashlib.md5(url.encode()).hexdigest()[:8]
    return os.path.join(PROFILES_DIR, f"{h}.json")


def script_exists(platform):
    return os.path.exists(get_platform_script_path(platform))


def generate_interaction_script(page, platform, url):
    """让 LLM 分析页面 DOM，生成该平台的交互脚本"""
    # 1. 提取页面关键 DOM（输入区 + 按钮区）
    dom_info = page.evaluate("""() => {
        let info = {url: location.href, title: document.title, inputArea: null, buttons: []};
        
        // 找输入框
        let inp = document.querySelector('[contenteditable=true], textarea, [role=textbox], #prompt-textarea');
        if (inp) {
            let p = inp;
            for (let i = 0; i < 4; i++) {
                if (p.parentElement) p = p.parentElement;
            }
            info.inputArea = {
                tag: inp.tagName,
                contenteditable: inp.getAttribute('contenteditable'),
                placeholder: inp.getAttribute('placeholder') || '',
                role: inp.getAttribute('role') || '',
                id: inp.id || '',
                className: (inp.className || '').substring(0, 200),
                parentHTML: (p.outerHTML || p.innerHTML || '').substring(0, 3000)
            };
        }
        
        // 找所有可能的按钮（仅输入区附近的，不是消息区的）
        let allBtns = document.querySelectorAll('[role=button], button');
        for (let b of allBtns) {
            let r = b.getBoundingClientRect();
            if (r.width > 10 && r.height > 10 && r.width < 100 && r.height < 100) {
                let hasSVG = !!b.querySelector('svg');
                let svgHTML = '';
                if (hasSVG) {
                    let svg = b.querySelector('svg');
                    svgHTML = (svg.outerHTML || '').substring(0, 300);
                }
                info.buttons.push({
                    tag: b.tagName,
                    role: b.getAttribute('role') || '',
                    ariaLabel: b.getAttribute('aria-label') || '',
                    dataTestId: b.getAttribute('data-testid') || '',
                    className: (b.className || '').substring(0, 200),
                    hasSVG: hasSVG,
                    svgPreview: svgHTML,
                    rect: {x: Math.round(r.x), y: Math.round(r.y), w: Math.round(r.width), h: Math.round(r.height)},
                    text: (b.innerText || '').substring(0, 50)
                });
            }
        }
        
        // 找弹窗/模态框
        let modals = document.querySelectorAll('[class*=dialog], [class*=modal], [class*=overlay], [class*=popup]');
        info.modals = [];
        for (let m of modals) {
            let r = m.getBoundingClientRect();
            if (r.width > 50 && r.height > 50) {
                info.modals.push({
                    className: (m.className || '').substring(0, 200),
                    text: (m.innerText || '').substring(0, 100)
                });
            }
        }
        
        return JSON.stringify(info);
    }""")

    print(f"[*] DOM 分析完成，正在让 LLM 生成 {platform} 交互脚本...")
    
    # 2. 调用 LLM 生成脚本
    script_content = _call_llm_generate(platform, url, dom_info)
    
    # 3. 保存脚本和 profile
    os.makedirs(PLATFORMS_DIR, exist_ok=True)
    os.makedirs(PROFILES_DIR, exist_ok=True)
    
    script_path = get_platform_script_path(platform)
    with open(script_path, "w", encoding="utf-8") as f:
        f.write(script_content)
    
    profile = {
        "url": url,
        "platform": platform,
        "generated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "dom_snapshot": dom_info[:500]
    }
    with open(get_profile_path(url), "w", encoding="utf-8") as f:
        json.dump(profile, f, ensure_ascii=False)
    
    print(f"[*] 脚本已缓存: {script_path}")
    return script_content


def _call_llm_generate(platform, url, dom_info):
    """调用 DeepSeek API 生成平台交互脚本。"""
    pitfalls_path = os.path.join(SCRIPTS_DIR, "..", "references", "pitfalls.md")
    pitfalls_text = ""
    if os.path.exists(pitfalls_path):
        with open(pitfalls_path, "r", encoding="utf-8") as f:
            pitfalls_text = f.read()

    system_prompt = f"""你是一个 Playwright 自动化脚本生成器。你需要基于给定的网页 DOM 信息，生成一个 Python 模块。

目标平台：{platform}
目标 URL：{url}

生成的模块必须包含以下常量和函数：

### 常量（必须）
- CAPABILITIES: 平台能力列表，如 ["text_input"] 或 ["text_input", "file_upload"]
- EXTRACT_SEL: AI 回复容器的 CSS 选择器字符串（用于提取 AI 回复内容）

### 函数（签名固定）
1. fill_prompt(page, prompt_text) -> bool
   - 定位输入框并填入文本
   - 返回是否成功
2. dismiss_blockers(page) -> None
   - 检测并关闭所有弹窗/对话框（DOM模态框 + 浏览器原生dialog）
   - 对原生 dialog 用 page.on("dialog", lambda d: d.accept())
3. submit(page) -> bool
   - 点击发送按钮发送消息
   - 必须从输入框出发找最近的发送按钮（避免点到历史消息的编辑按钮）
   - 优先点击按钮发送，Enter 键作为兜底
   - 返回是否发送成功
4. upload_file(page, file_path) -> bool
   - 上传文件到当前对话（L3 整合阶段需要）
   - 策略 1: input[type="file"] → page.locator('input[type="file"]').first.set_input_files(file_path)
   - 策略 2: 上传按钮 → 点击触发 file_chooser → file_chooser.set_files(file_path)
   - 如果 DOM 中有 input[type="file"]，必须生成此函数
   - 返回是否上传成功

规则：
- 发送键：优先点击按钮，Enter 键作为兜底（注意：Quill/ProseMirror 编辑器中 Enter 是换行而非发送）
- 弹窗处理：每次操作前后都要清弹窗
- 按钮定位：优先用 aria-label、data-testid，其次用 class 名匹配，再次用位置启发式（输入框右下角的按钮）
- 深度搜索优先使用 Playwright 的 JavaScript 注入
- 纯 Python 代码，不要 markdown 代码块标记
- 导入：from playwright.sync_api import Page
- 编码声明：# -*- coding: utf-8 -*-
- 只输出代码，不要任何解释
- 纯中文注释

## 平台特定规则（通用，所有平台都适用）
1. 发送按钮：优先查找 aria-label 含 "发送"/"Send"/"Submit" 的按钮，或 data-testid 含 "send" 的按钮
2. 发送后：绝对不要按 Escape 键（会被解释为"停止生成"），dismiss_blockers 只在发送前调用
3. 发送验证：输入框残留 <= 5 字符即视为成功，不要要求完全清零
4. 输入框选择器：优先 [role=textbox]，其次 [contenteditable=true]，最后 textarea

## 已知陷阱（必须遵守）
{ pitfalls_text }
"""

    user_prompt = f"""以下是网页的 DOM 结构信息（JSON）：

{dom_info}

请基于这些信息生成 {platform} 平台的交互模块代码。
重点分析：
1. 输入框的确切选择器（tag、class、role 等特征）
2. 发送按钮的位置特征（相对于输入框的位置、SVG图标特征、class名）
3. 可能的弹窗类型和关闭方式
4. 发送后页面的变化特征（输入框是否清空、是否出现新消息）

生成完整的 Python 模块代码："""

    try:
        from common import call_deepseek_api
        content = call_deepseek_api(
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            model="deepseek-v4-pro",
            max_tokens=4096,
            temperature=0.1,
            timeout=120,
        )
        if not content:
            raise RuntimeError("API 返回空")
        # 清理 markdown 代码块
        if content.startswith("```"):
            lines = content.split("\n")
            content = "\n".join(lines[1:]) if lines[0].startswith("```") else content
            if content.endswith("```"):
                content = content[:-3].strip()
        return content
    except Exception as e:
        print(f"    LLM 生成失败: {e}")
        print(f"[!] 降级到通用兜底模板 -- 发送/提取不受影响，但平台适配可能不完美")
        return _fallback_template(platform)


def _generate_script_with_test_result(platform, url, dom_analysis, test_question,
                                       extracted_response, fill_result, submit_result,
                                       upload_result=None, file_test_response=None):
    """基于 DOM 分析 + 实测结果，让 LLM 分析并生成最终交互脚本。

    这是定型的核心：不仅看 DOM，还看实际发送接收是否成功，
    LLM 综合两者判断哪些选择器真正有效，生成经过验证的脚本。

    upload_result: 文件上传测试结果（True/False/None=未测试）
    file_test_response: 文件上传+文字发送后的 AI 回复内容（str/None）
    """
    dom_json = json.dumps(dom_analysis, ensure_ascii=False, indent=2)

    # 文件上传能力判定
    has_file_input = len(dom_analysis.get("fileInputs", [])) > 0
    has_upload_btn = len(dom_analysis.get("uploadButtons", [])) > 0
    upload_capable = has_file_input or has_upload_btn
    upload_status = "未测试" if upload_result is None else ("✓ 成功" if upload_result else "✗ 失败")

    # 文件上传完整测试（上传+文字+发送+回复）结果
    if file_test_response:
        upload_test_block = f"""文件上传：{upload_status}（DOM 中 {'有' if upload_capable else '无'} input[type=file]，{'有' if has_upload_btn else '无'}上传按钮）
文件+文字完整测试：✓ 成功
AI 回复内容（前 1500 字符）：
{file_test_response[:1500]}"""
    else:
        upload_test_block = f"文件上传：{upload_status}（DOM 中 {'有' if upload_capable else '无'} input[type=file]，{'有' if has_upload_btn else '无'}上传按钮）"

    # 文字实测结果格式化（仅在测试过时有意义）
    if test_question is not None:
        text_test_block = f"""实测问题：{test_question}
fill_prompt 结果：{fill_result}
submit 结果：{submit_result}
AI 回复内容（前 3000 字符）：
{extracted_response[:3000] if extracted_response else '（空）'}"""
    else:
        text_test_block = "（文字收发未测试，仅基于 DOM 分析生成脚本）"

    system_prompt = f"""你是一个 Playwright 自动化脚本生成器。你需要基于网页 DOM 分析和实测验证结果，生成一个经过验证的 Python 平台交互模块。

目标平台：{platform}
目标 URL：{url}

## 实测验证结果（关键参考）

{text_test_block}
{upload_test_block}

## 任务

{"请分析以上实测结果：\n1. 判断 AI 回复是否真正回答了测试问题（回复内容是否合理）\n2. 如果实测成功，说明当前使用的选择器是正确的，应该在最终脚本中保留\n3. 如果实测有问题，分析可能的原因并提出改进" if test_question is not None else "请基于 DOM 分析结果生成平台交互脚本。\n由于未进行文字收发实测，请根据 DOM 特征推断合理的选择器。"}

然后生成最终的 Python 模块，必须包含以下内容：

### 常量（必须）

```python
CAPABILITIES = ["text_input"]  # 平台能力列表，可选: "text_input", "file_upload"
EXTRACT_SEL = "..."  # AI 回复容器的 CSS 选择器（用于 dom_extract 提取回复内容）
```

EXTRACT_SEL 应根据 DOM 分析中 AI 回复所在容器的 className/id 来编写。
例如 DeepSeek 用 'div[class*="message"][class*="assistant"]'，ChatGPT 用 'div[data-message-author-role="assistant"]'。

{"如果文件上传测试成功，CAPABILITIES 必须包含 \"file_upload\"。" if upload_result else ""}

### 函数（必须）

1. fill_prompt(page, prompt_text) -> bool
   - 定位输入框并填入文本
   - 必须基于实测成功的 DOM 特征来编写选择器
   - 返回是否成功

2. dismiss_blockers(page) -> None
   - 检测并关闭所有弹窗/对话框
   - 对原生 dialog 用 page.on("dialog", lambda d: d.accept())

3. submit(page) -> bool
   - 点击发送按钮
   - 必须从输入框出发找最近的发送按钮
   - 优先点击按钮发送，Enter 键作为兜底（注意：某些编辑器中 Enter 是换行而非发送）
   - 返回是否发送成功
{"""
4. upload_file(page, file_path) -> bool
   - 上传文件到当前对话
   - 策略 1: 找 input[type="file"] → page.locator('input[type="file"]').first.set_input_files(file_path)
   - 策略 2: 找上传按钮 → 点击触发 file_chooser → file_chooser.set_files(file_path)
   - 返回是否上传成功
""" if upload_result else ""}
## 规则

- 优先用实测验证过的选择器，不要凭空猜测
- 发送按钮：优先 aria-label 含 "发送"/"Send"/"Submit" 的按钮，或 data-testid 含 "send" 的按钮
- 发送后：绝对不要按 Escape 键（会被解释为"停止生成"）
- 发送验证：输入框残留 <= 5 字符视为成功
- 纯 Python 代码，不要 markdown 代码块标记
- 导入：from playwright.sync_api import Page
- 编码声明：# -*- coding: utf-8 -*-
- 只输出代码，不要任何解释
- 纯中文注释
"""

    user_prompt = f"""## 网页 DOM 分析

{dom_json[:8000]}

## 实测验证数据

{text_test_block}
- {upload_test_block}

请基于以上信息，生成 {platform} 平台的最终交互脚本。
重点：
1. {"根据实测结果判断哪些选择器是正确的，将其写入最终脚本" if test_question is not None else "根据 DOM 分析推断合理的选择器"}
2. 输入框选择器必须精确（基于 DOM 分析中的 className/id/placeholder 等特征）
3. 发送按钮选择器必须精确（基于实测成功的按钮特征）
4. EXTRACT_SEL 必须精确指向 AI 回复容器（基于 DOM 分析中 className 含 "message"/"response"/"assistant" 的元素）
{"5. 文件上传已测试成功，必须生成 upload_file() 函数（基于 DOM 中的 input[type=file] 或上传按钮特征）" if upload_result else ""}

生成完整的 Python 模块代码："""

    try:
        from common import call_deepseek_api
        content = call_deepseek_api(
            system_prompt=system_prompt,
            user_prompt=user_prompt,
            model="deepseek-v4-pro",
            max_tokens=4096,
            temperature=0.1,
            timeout=120,
        )
        if not content:
            raise RuntimeError("API 返回空")
        # 清理 markdown 代码块
        if content.startswith("```"):
            lines = content.split("\n")
            content = "\n".join(lines[1:]) if lines[0].startswith("```") else content
            if content.endswith("```"):
                content = content[:-3].strip()
        return content
    except Exception as e:
        print(f"    LLM 生成失败: {e}")
        print(f"[!] 降级到通用兜底模板")
        return _fallback_template(platform)


def _fallback_template(platform):
    """LLM 不可用时的兜底模板"""
    return _generic_template()


def _generic_template():
    return '''# -*- coding: utf-8 -*-
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

'''
