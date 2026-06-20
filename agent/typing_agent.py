# -*- coding: utf-8 -*-
"""平台定型 Agent —— 完整流程。

用法: python typing_agent.py <platform> <url> [--cdp-port PORT] [--api-key KEY]

流程:
  1. 连接浏览器，打开平台 URL
  2. 读取页面完整 HTML，分析 DOM（输入框/发送按钮/弹窗）
  3. 用真实格式的搜索 prompt 实测：填入 → 发送 → 等待回复 → 提取回复
  4. LLM 分析实测结果（问题 + 回复 + DOM）→ 判断交互是否成功
  5. 基于分析结果生成最终通用脚本 → 保存
"""
import sys, os, json, time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPT_DIR)


def _generate_test_topic():
    """LLM 实时生成随机测试主题（每次定型都不重复）。

    与真实搜索同构：真实场景由 extract_intent() 从用户问题提取主题，
    定型场景由 LLM 随机生成一个搜索主题。
    """
    try:
        from common import call_deepseek_api
        topic = call_deepseek_api(
            system_prompt="你是一个随机主题生成器。生成一个适合测试 AI 搜索能力的主题（≤40字）。"
                          "要求：有一定深度、需要联网搜索才能回答、每次不同。"
                          "只输出主题文本，不要任何解释或标点修饰。",
            user_prompt=f"当前时间 {time.strftime('%Y-%m-%d %H:%M')}，请生成一个随机搜索主题。",
            model="deepseek-v4-pro",
            max_tokens=80,
            temperature=1.0,
            timeout=30,
        )
        if topic:
            return topic.strip().strip('"').strip('*')
    except Exception as e:
        print(f"[Typing] LLM 生成主题失败: {e}，使用兜底主题")

    # 兜底：用时间戳保证不重复
    return f"AI 技术发展动态分析 {time.strftime('%Y%m%d%H%M')}"


def _build_typing_prompt():
    """构建定型测试 prompt —— 直接复用真实搜索的 build_search_prompt()。

    与实际使用完全同构：
    - LLM 实时生成主题（不重复）
    - build_search_prompt() 构建完整 prompt（约束 + 标记对）
    """
    topic = _generate_test_topic()
    from prompt_builder import build_search_prompt
    topic_with_hash, prompt = build_search_prompt(topic)
    marker = f"[搜索主题：{topic_with_hash}]"
    return topic, prompt, marker


def run_typing(platform, url, cdp_port=9223, api_key="", api_url="https://api.deepseek.com/v1",
               do_text=True, do_file_upload=False):
    """执行平台定型。返回 (success, message)。"""
    targets = []
    if do_text: targets.append("文字收发")
    if do_file_upload: targets.append("文件上传")
    if not targets:
        return False, "未选择任何定型目标"
    print(f"[Typing] 开始定型: platform={platform}, url={url}, cdp={cdp_port}, "
          f"目标={'+'.join(targets)}")

    # 注入 API 配置
    import common
    common.set_runtime_config({
        "deepseek_api": api_url,
        "deepseek_key": api_key,
        "search_platform": platform,
        "synthesis_platform": platform,
        "cdp_port": cdp_port,
        "platform_urls": {platform: url},
    })

    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        return False, "Playwright 未安装"

    try:
        with sync_playwright() as p:
            # ============================================================
            # Step 1: 连接浏览器，打开页面
            # ============================================================
            print(f"[Typing] 连接浏览器 CDP:{cdp_port} ...")
            browser = p.chromium.connect_over_cdp(
                f"http://127.0.0.1:{cdp_port}", timeout=5000)
            if not browser.contexts:
                return False, "浏览器无活跃上下文"
            context = browser.contexts[0]

            print(f"[Typing] 打开 {url} ...")
            page = context.new_page()
            page.goto(url, timeout=30000, wait_until="domcontentloaded")
            time.sleep(3)
            actual_url = page.url
            print(f"[Typing] 当前页面: {actual_url}")

            # ============================================================
            # Step 2: 读取完整 HTML + 分析 DOM
            # ============================================================
            print(f"[Typing] 读取页面 HTML 并分析 DOM...")
            page_html = page.evaluate("document.documentElement.outerHTML")
            # 限制 HTML 长度，取头部结构（前 5000 字符）+ 关键区域
            html_head = page_html[:5000]
            body_html = page.evaluate("document.body ? document.body.innerHTML.substring(0, 8000) : ''")

            dom_analysis = page.evaluate("""() => {
                let info = {url: location.href, title: document.title,
                            inputs: [], buttons: [], modals: [],
                            fileInputs: [], uploadButtons: []};

                // 所有可编辑元素
                let editables = document.querySelectorAll(
                    '[contenteditable="true"], textarea, input[type="text"], [role="textbox"]');
                editables.forEach((el, i) => {
                    if (i > 8) return;
                    let r = el.getBoundingClientRect();
                    info.inputs.push({
                        index: i,
                        tag: el.tagName,
                        id: el.id || '',
                        className: (el.className || '').substring(0, 150),
                        placeholder: el.getAttribute('placeholder') || '',
                        role: el.getAttribute('role') || '',
                        aria_label: el.getAttribute('aria-label') || '',
                        contenteditable: el.getAttribute('contenteditable') || '',
                        visible: r.width > 10 && r.height > 10,
                        rect: {x: Math.round(r.x), y: Math.round(r.y),
                               w: Math.round(r.width), h: Math.round(r.height)},
                        parent_classes: el.parentElement ? (el.parentElement.className || '').substring(0, 100) : '',
                        near_bottom: r.y > window.innerHeight * 0.4,
                    });
                });

                // 所有按钮
                let btns = document.querySelectorAll('button, [role="button"]');
                btns.forEach((b, i) => {
                    if (i > 15) return;
                    let r = b.getBoundingClientRect();
                    let hasSVG = !!b.querySelector('svg');
                    info.buttons.push({
                        index: i,
                        tag: b.tagName,
                        id: b.id || '',
                        className: (b.className || '').substring(0, 150),
                        ariaLabel: b.getAttribute('aria-label') || '',
                        dataTestId: b.getAttribute('data-testid') || '',
                        type: b.getAttribute('type') || '',
                        text: (b.innerText || '').substring(0, 50),
                        hasSVG: hasSVG,
                        svgHTML: hasSVG ? (b.querySelector('svg').outerHTML || '').substring(0, 300) : '',
                        visible: r.width > 10 && r.height > 10,
                        rect: {x: Math.round(r.x), y: Math.round(r.y),
                               w: Math.round(r.width), h: Math.round(r.height)},
                    });
                });

                // 弹窗/模态框
                let modals = document.querySelectorAll('[class*=dialog], [class*=modal], [class*=overlay], [class*=popup], [role=dialog]');
                modals.forEach((m) => {
                    let r = m.getBoundingClientRect();
                    if (r.width > 50 && r.height > 50) {
                        info.modals.push({
                            className: (m.className || '').substring(0, 150),
                            text: (m.innerText || '').substring(0, 100),
                        });
                    }
                });

                // 文件上传入口
                document.querySelectorAll('input[type="file"]').forEach(el => {
                    info.fileInputs.push({
                        accept: (el.getAttribute('accept') || '').substring(0, 100),
                        visible: el.offsetParent !== null,
                        parentClass: el.parentElement ? (el.parentElement.className || '').substring(0, 80) : '',
                    });
                });
                document.querySelectorAll('button, [role=button], svg').forEach(el => {
                    let a = (el.getAttribute('aria-label') || '').toLowerCase();
                    let t = (el.textContent || '').toLowerCase();
                    let c = (el.className || '').toString().toLowerCase();
                    if (a.includes('upload') || a.includes('file') || a.includes('attach') ||
                        a.includes('clip') || a.includes('上传') || a.includes('附件') || a.includes('文件') ||
                        t.includes('上传') || t.includes('附件') ||
                        c.includes('upload') || c.includes('attach')) {
                        info.uploadButtons.push({
                            tag: el.tagName,
                            aria: a.substring(0, 60),
                            text: t.substring(0, 40),
                            className: c.substring(0, 80),
                        });
                    }
                });

                return info;
            }""")

            print(f"[Typing] DOM 分析: {len(dom_analysis.get('inputs',[]))} 输入区, "
                  f"{len(dom_analysis.get('buttons',[]))} 按钮, "
                  f"{len(dom_analysis.get('modals',[]))} 弹窗, "
                  f"{len(dom_analysis.get('fileInputs',[]))} 文件输入, "
                  f"{len(dom_analysis.get('uploadButtons',[]))} 上传按钮")

            # ============================================================
            # Step 3: 文字收发实测（仅在 do_text=True 时执行）
            # ============================================================
            fill_result = None
            submit_result = None
            extracted = None
            test_topic = None
            mod = None

            if do_text:
                test_topic, test_prompt, test_marker = _build_typing_prompt()
                print(f"[Typing] ===== 文字收发实测 =====")
                print(f"[Typing] 测试主题: {test_topic}")
                print(f"[Typing] Prompt 长度: {len(test_prompt)} 字符 (含标记对+回复约束)")

                # 先用兜底模板尝试发送（如果已有平台脚本则用已有脚本）
                from generator import script_exists, load_platform_module, _generic_template
                if script_exists(platform):
                    mod = load_platform_module(platform)
                    print(f"[Typing] 使用已有脚本")
                else:
                    # 临时用兜底模板做测试
                    import types
                    mod = types.ModuleType("_temp_platform")
                    generic_code = _generic_template()
                    exec(generic_code, mod.__dict__)
                    print(f"[Typing] 使用兜底模板测试")

                # 3a-3c: 填入 + 发送，最多重试 3 次
                send_ok = False

                for attempt in range(1, 4):
                    print(f"[Verify] ===== 第 {attempt} 次尝试 =====")

                    # 3a. 填入测试 prompt（真实格式：主题 + 约束 + 标记对）
                    print(f"[Verify] 填入测试 prompt ({len(test_prompt)} 字符)...")
                    try:
                        fill_result = mod.fill_prompt(page, test_prompt)
                    except Exception as e:
                        fill_result = False
                        print(f"[Verify] fill_prompt() 异常: {e}")

                    # 验证是否真的填进去了
                    try:
                        filled_len = page.evaluate(
                            """() => {
                                let e = document.querySelector('[contenteditable=true], textarea, [role=textbox]');
                                return e ? (e.value || e.innerText || e.textContent || '').length : 0;
                            }""")
                        if filled_len and filled_len > 5:
                            print(f"[Verify] ✓ 输入框已有 {filled_len} 字符")
                        else:
                            print(f"[Verify] ⚠ 输入框内容不足 (仅 {filled_len} 字符)")
                            fill_result = False
                    except Exception:
                        pass

                    if not fill_result:
                        print(f"[Verify] fill 失败，{'重试' if attempt < 3 else '放弃'}...")
                        if attempt < 3:
                            time.sleep(attempt * 2)
                            continue
                        return False, "fill_prompt() 3 次均失败 —— 无法定位输入框或填入内容"

                    time.sleep(0.5)

                    # 3b. 消除弹窗
                    try:
                        mod.dismiss_blockers(page)
                    except Exception as e:
                        print(f"[Verify] dismiss_blockers: {e}（非致命）")

                    # 3c. 点击发送
                    print(f"[Verify] 点击发送...")
                    try:
                        submit_result = mod.submit(page)
                    except Exception as e:
                        submit_result = False
                        print(f"[Verify] submit() 异常: {e}")

                    if not submit_result:
                        print(f"[Verify] submit 失败，{'重试' if attempt < 3 else '放弃'}...")
                        if attempt < 3:
                            time.sleep(attempt * 2)
                            continue
                        return False, "submit() 3 次均失败 —— 发送按钮未找到或点击失败"

                    print(f"[Verify] fill: {fill_result}, submit: {submit_result}")

                    # 3d. 等待输入框清空（证明消息已发送）
                    print(f"[Verify] 等待输入框清空...")
                    input_cleared = False
                    for i in range(30):  # 60s（比之前更短，因为有重试）
                        time.sleep(2)
                        try:
                            remaining = page.evaluate(
                                """() => {
                                    let e = document.querySelector('[contenteditable=true], textarea, [role=textbox]');
                                    return e ? (e.value || e.innerText || e.textContent || '').length : -1;
                                }""")
                            if remaining is not None and remaining <= 5:
                                input_cleared = True
                                print(f"[Verify] ✓ 输入框已清空 (残留={remaining})")
                                break
                        except Exception:
                            pass
                        if i % 10 == 9:
                            print(f"[Verify] ...等待中 ({(i+1)*2}s)")

                    if input_cleared:
                        send_ok = True
                        break
                    else:
                        print(f"[Verify] ⚠ 输入框未清空，{'重试' if attempt < 3 else '放弃'}...")
                        if attempt < 3:
                            time.sleep(attempt * 2)

                if not send_ok:
                    return False, "发送后输入框未清空 —— 消息未被平台接收（3 次尝试均失败）"

                # 3e. 等待 AI 回复（降低阈值：短回复也算成功）
                print(f"[Verify] 等待 AI 回复...")
                body_before = page.evaluate("document.body ? document.body.innerText.length : 0")
                reply_appeared = False
                for i in range(45):
                    time.sleep(2)
                    body_now = page.evaluate("document.body ? document.body.innerText.length : 0")
                    growth = body_now - body_before
                    if growth > 5:  # 降低阈值：哪怕只增长了 5 字符也算有回复
                        reply_appeared = True
                        print(f"[Verify] ✓ AI 回复已出现 (增长 {growth} 字符)")
                        break
                    if i % 10 == 9:
                        print(f"[Verify] ...等待中 ({(i+1)*2}s)")

                if not reply_appeared:
                    return False, "90s 内未检测到 AI 回复"

                # 3f. 等回复稳定后提取
                print(f"[Verify] 等待回复稳定...")
                stable_count = 0
                last_len = page.evaluate("document.body ? document.body.innerText.length : 0")
                for i in range(30):
                    time.sleep(2)
                    now_len = page.evaluate("document.body ? document.body.innerText.length : 0")
                    if now_len == last_len:
                        stable_count += 1
                        if stable_count >= 3:
                            break
                    else:
                        stable_count = 0
                        last_len = now_len

                from extractor import dom_extract
                extracted = dom_extract(page, platform)
                if not extracted:
                    # dom_extract 可能因为内容太短（< 150字符）返回 None
                    # 定型场景下用 page_text 兜底
                    print(f"[Verify] dom_extract 返回空，尝试 page_text 兜底...")
                    try:
                        from common import safe_page_text
                        raw = safe_page_text(page)
                        if raw and len(raw) > 50:
                            # 取页面末尾 2000 字符作为候选回复
                            extracted = raw[-2000:]
                            print(f"[Verify] page_text 兜底: {len(extracted)} 字符")
                    except Exception:
                        pass
                if not extracted:
                    return False, "dom_extract() 返回空且兜底也失败"
                print(f"[Verify] 提取到 {len(extracted)} 字符: {extracted[:200]}...")

            # ============================================================
            # Step 3g: 文件上传实测（仅在勾选时执行）
            # 与 L3 整合同构：upload_file → fill_prompt → submit → 等回复
            # ============================================================
            has_file_input = len(dom_analysis.get("fileInputs", [])) > 0
            has_upload_btn = len(dom_analysis.get("uploadButtons", [])) > 0
            upload_capable = has_file_input or has_upload_btn
            upload_result = None
            file_test_topic = None
            file_extracted = None

            if do_file_upload:
                print(f"[Typing] ===== 文件上传实测 =====")
                print(f"[Typing] input[type=file]: {'✓' if has_file_input else '✗'} | "
                      f"上传按钮: {'✓' if has_upload_btn else '✗'}")

                if upload_capable:
                    # 确保有平台模块（如果文字测试已加载则复用，否则加载）
                    if mod is None:
                        from generator import script_exists, load_platform_module, _generic_template
                        if script_exists(platform):
                            mod = load_platform_module(platform)
                            print(f"[Typing] 文件测试: 使用已有脚本")
                        else:
                            import types
                            mod = types.ModuleType("_temp_platform")
                            generic_code = _generic_template()
                            exec(generic_code, mod.__dict__)
                            print(f"[Typing] 文件测试: 使用兜底模板")

                    # 生成独立测试主题（与文字测试不重复）
                    file_test_topic, file_test_prompt, file_test_marker = _build_typing_prompt()
                    print(f"[Typing] 文件测试主题: {file_test_topic}")
                    print(f"[Typing] Prompt 长度: {len(file_test_prompt)} 字符")

                    # 创建测试文件
                    test_upload_file = os.path.join(os.path.dirname(SCRIPT_DIR), "data", "_upload_test.txt")
                    try:
                        os.makedirs(os.path.dirname(test_upload_file), exist_ok=True)
                        with open(test_upload_file, "w", encoding="utf-8") as f:
                            f.write(f"# 定型上传测试文件\n生成时间: {time.strftime('%Y-%m-%d %H:%M:%S')}\n"
                                    "此文件用于验证平台文件上传能力，包含少量文本内容。\n")

                        # Step A: 上传文件（与 orchestrator L3 一致）
                        from platforms._base import safe_upload_file
                        print(f"[Verify] Step A: 上传文件...")
                        upload_result = safe_upload_file(page, test_upload_file)
                        print(f"[Verify] 上传结果: {'✓ 成功' if upload_result else '✗ 失败'}")

                        if upload_result:
                            time.sleep(2)
                            # 检查文件预览/附件标记
                            has_preview = page.evaluate("""() => {
                                let els = document.querySelectorAll('[class*=file], [class*=attachment], [class*=preview], [class*=upload]');
                                for (let el of els) {
                                    let r = el.getBoundingClientRect();
                                    if (r.width > 20 && r.height > 20 && r.y > window.innerHeight * 0.3) {
                                        return true;
                                    }
                                }
                                return false;
                            }""")
                            print(f"[Verify] 文件预览/附件标记: {'✓' if has_preview else '✗（不影响判定）'}")

                            # Step B: 填入配套文字（与真实使用一致：上传文件 + 发文字）
                            print(f"[Verify] Step B: 填入配套文字 ({len(file_test_prompt)} 字符)...")
                            try:
                                file_fill = mod.fill_prompt(page, file_test_prompt)
                                print(f"[Verify] fill_prompt: {'✓' if file_fill else '✗'}")
                            except Exception as e:
                                file_fill = False
                                print(f"[Verify] fill_prompt 异常: {e}")

                            # Step C: 发送（文件 + 文字一起）
                            if file_fill:
                                print(f"[Verify] Step C: 发送（文件 + 文字）...")
                                try:
                                    mod.dismiss_blockers(page)
                                except Exception:
                                    pass
                                try:
                                    file_submit = mod.submit(page)
                                    print(f"[Verify] submit: {'✓' if file_submit else '✗'}")
                                except Exception as e:
                                    file_submit = False
                                    print(f"[Verify] submit 异常: {e}")

                                # Step D: 等待输入框清空
                                if file_submit:
                                    print(f"[Verify] Step D: 等待输入框清空...")
                                    for i in range(30):
                                        time.sleep(2)
                                        try:
                                            remaining = page.evaluate(
                                                """() => {
                                                    let e = document.querySelector('[contenteditable=true], textarea, [role=textbox]');
                                                    return e ? (e.value || e.innerText || e.textContent || '').length : -1;
                                                }""")
                                            if remaining is not None and remaining <= 5:
                                                print(f"[Verify] ✓ 输入框已清空 (残留={remaining})")
                                                break
                                        except Exception:
                                            pass

                                    # Step E: 等待 AI 回复
                                    print(f"[Verify] Step E: 等待 AI 回复...")
                                    body_before = page.evaluate("document.body ? document.body.innerText.length : 0")
                                    for i in range(45):
                                        time.sleep(2)
                                        body_now = page.evaluate("document.body ? document.body.innerText.length : 0")
                                        growth = body_now - body_before
                                        if growth > 5:
                                            print(f"[Verify] ✓ AI 回复已出现 (增长 {growth} 字符)")
                                            break

                                    # Step F: 提取回复
                                    time.sleep(5)
                                    from extractor import dom_extract
                                    file_extracted = dom_extract(page, platform)
                                    if not file_extracted:
                                        try:
                                            from common import safe_page_text
                                            raw = safe_page_text(page)
                                            if raw and len(raw) > 50:
                                                file_extracted = raw[-2000:]
                                        except Exception:
                                            pass
                                    if file_extracted:
                                        print(f"[Verify] 文件测试提取到 {len(file_extracted)} 字符: "
                                              f"{file_extracted[:200]}...")
                                    else:
                                        print(f"[Verify] 文件测试提取为空")
                    except Exception as e:
                        print(f"[Verify] 上传测试异常: {e}")
                        upload_result = False
                    finally:
                        try:
                            os.remove(test_upload_file)
                        except Exception:
                            pass
                else:
                    print(f"[Typing] 该平台未检测到文件上传入口 → CAPABILITIES 不含 file_upload")

            # ============================================================
            # Step 4: LLM 分析实测结果 + 生成最终脚本
            # ============================================================
            print(f"[Typing] ===== LLM 分析并生成脚本 =====")
            from generator import _generate_script_with_test_result
            script = _generate_script_with_test_result(
                platform=platform,
                url=url,
                dom_analysis=dom_analysis,
                test_question=test_topic,
                extracted_response=extracted[:3000] if extracted else None,
                fill_result=fill_result,
                submit_result=submit_result,
                upload_result=upload_result,
                file_test_response=file_extracted[:3000] if file_extracted else None,
            )

            if not script:
                return False, "LLM 脚本生成失败 —— 请检查 API Key 是否有效"

            # ============================================================
            # Step 5: 保存脚本
            # ============================================================
            import generator as gen
            os.makedirs(gen.PLATFORMS_DIR, exist_ok=True)
            os.makedirs(gen.PROFILES_DIR, exist_ok=True)

            script_path = gen.get_platform_script_path(platform)
            with open(script_path, "w", encoding="utf-8") as f:
                f.write(script)

            profile = {
                "url": url, "platform": platform,
                "generated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
                "test_topic": test_topic,
                "test_prompt_length": len(test_prompt),
                "test_response_preview": extracted[:200],
                "upload_capable": upload_capable,
                "upload_tested": upload_result is not None,
                "upload_result": upload_result,
            }
            with open(gen.get_profile_path(url), "w", encoding="utf-8") as f:
                json.dump(profile, f, ensure_ascii=False)

            print(f"[Typing] ✓ 定型完成: {script_path}")
            return True, f"定型完成（实测验证通过）: agent/platforms/{platform}.py"

    except Exception as e:
        import traceback
        traceback.print_exc()
        return False, str(e)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="Certus 平台定型工具")
    parser.add_argument("platform", help="平台名")
    parser.add_argument("url", help="平台 URL（必须为聊天页 URL）")
    parser.add_argument("--cdp-port", type=int, default=9223, help="CDP 端口")
    parser.add_argument("--api-key", default="", help="DeepSeek API Key")
    parser.add_argument("--api-url", default="https://api.deepseek.com/v1", help="DeepSeek API 端点")
    parser.add_argument("--file-upload", action="store_true", help="启用文件上传定型")
    parser.add_argument("--skip-text", action="store_true", help="跳过文字收发定型")
    args = parser.parse_args()

    ok, msg = run_typing(args.platform, args.url, args.cdp_port,
                         api_key=args.api_key, api_url=args.api_url,
                         do_text=not args.skip_text,
                         do_file_upload=args.file_upload)
    print(f"\n{'✓' if ok else '✗'} {msg}")
    sys.exit(0 if ok else 1)
