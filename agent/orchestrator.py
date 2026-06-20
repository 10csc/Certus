# -*- coding: utf-8 -*-
"""执行编排层。
双平台体系：
  - 检索平台：发送搜索 → 轮询提取
  - 整合平台：汇总素材 → 生成报告

平台配置在 config.json：search_platform / synthesis_platform / platform_urls
加新平台只需改 config.json，零代码改动。
"""

import os, sys, time, re, threading

# C++ 取消信号（跨线程通信）
try:
    from agent_protocol import cancel_flag
except ImportError:
    cancel_flag = threading.Event()  # CLI 模式回退

class ConfigError(Exception):
    """配置错误——重试无意义，应立即终止。"""
    pass

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from common import (
    ensure_browser, ensure_page, safe_page_text,
    get_session_url, is_valid_session_url, DEFAULT_CDP_PORT,
    submit_and_verify, save_result,
    get_search_platform, get_synthesis_platform,
)
from generator import load_platform_module
from prompt_builder import build_search_prompt, build_synthesis_prompt, validate_prompt, assess_reliability
from extractor import extract_with_diagnosis, is_content_complete
from synthesizer import analyze_sources
from logger import log_entry
from diagnostics import diagnose, format_diagnosis


GAP_KEYWORDS = [
    "需要进一步了解", "建议查阅", "建议参考",
    "未覆盖", "未涉及", "超出范围", "不确定", "存疑",
    "有待验证", "需要确认", "暂不明确", "信息不足",
]


def detect_gaps(content):
    if not content: return []
    gaps = []
    for kw in GAP_KEYWORDS:
        if kw in content:
            idx = content.find(kw)
            ctx = content[max(0,idx-20):idx+len(kw)+60].replace("\n"," ").strip()
            gaps.append(f"[{kw}] {ctx}...")
    return gaps


def extract_links(content):
    """从内容中提取所有 URL。同时从 [已确认:URL] [已验证:URL] 标记中提取。"""
    if not content: return []
    # 1. 通用 URL 正则
    urls = re.findall(r'https?://[^\s<>"\')\]]+', content)
    # 2. 从可靠性标记中提取 [已确认:URL] [已验证:URL]
    marker_urls = re.findall(r'\[(?:已确认|已验证)\s*:\s*(https?://[^\]]+)\]', content)
    urls.extend(marker_urls)
    seen, result = set(), []
    for u in urls:
        u = u.rstrip(".,;: ")
        if u not in seen and len(u) > 20:
            seen.add(u); result.append(u)
    return result[:30]


def extract_named_sources(content):
    """从 [已确认:名称] [已验证:名称] 中提取具名信源（非URL但可分类）。

    用于信源分布统计——即使 AI 没给 URL，也能从信源名称推分类。
    """
    if not content: return []
    # 匹配 [已确认:xxx] 或 [已验证:xxx]，其中 xxx 不是 URL
    markers = re.findall(
        r'\[(?:已确认|已验证)\s*:\s*([^]]+)\]', content
    )
    sources = []
    seen = set()
    for m in markers:
        m = m.strip()
        # 跳过纯 URL（由 extract_links 处理）和太短的
        if m.startswith("http") or len(m) < 3 or m in seen:
            continue
        seen.add(m)
        # 用 URL 分类器对名称做启发式分类
        from synthesizer import classify_source
        cat = classify_source(m.lower())
        sources.append({"url": f"named:{m}", "category": cat, "score": 4})
    return sources


def _synthesize_local(materials, user_query):
    """本地 API 总结 — 纯文本调用，不触发搜索循环。"""
    from common import call_deepseek_api
    return call_deepseek_api(
        system_prompt="你是技术报告生成器。基于采集素材生成完整研究报告（概述、核心发现、方案对比、风险评估）。纯 Markdown，中文。",
        user_prompt=f"原始问题：{user_query}\n\n采集素材：\n{materials[:60000]}\n\n请基于以上素材生成完整研究报告。注意：只做总结归纳，不发起新的联网搜索。",
        model="deepseek-v4-pro",
        max_tokens=8192,
        temperature=0.3,
        timeout=120,
    )


def _is_valid_synthesis(content, max_chars=2000):
    """LLM 判断合成内容是否有效（非拒绝/道歉/空话）。返回 (bool, str)。"""
    from common import call_deepseek_api
    sample = content[:max_chars]
    answer = call_deepseek_api(
        system_prompt="你是一个内容质量检测器。只回复 YES 或 NO。",
        user_prompt=f"以下文本是否是一份有效的研究整合报告（包含分析、结论、验证标注）？如果文本主要是道歉、拒绝、高峰期提示、或只有搜索URL列表没有分析，回复 NO。\n\n{sample}",
        model="deepseek-v4-pro",
        max_tokens=2,
        temperature=0.0,
        timeout=30,
    )
    if answer is None:
        return False, "检测异常(API调用失败)，默认拒绝"
    return answer.strip().upper().startswith("YES"), f"LLM判定: {answer.strip()}"


def _evolve_upload(platform, page):
    """自进化：诊断页面文件上传能力，持久化到平台档案。"""
    from evolution import load_or_create_profile
    import json as _json
    try:
        profile = load_or_create_profile(platform)
        upload_info = page.evaluate("""() => {
            let r = {fileInputs: [], uploadButtons: [], method: 'unknown'};
            document.querySelectorAll('input[type="file"]').forEach(el => {
                r.fileInputs.push({
                    accept: el.getAttribute('accept')?.substring(0,100)||'',
                    visible: el.offsetParent !== null,
                    parentClass: el.parentElement?.className?.substring(0,60)||''
                });
            });
            // 找上传图标/按钮
            document.querySelectorAll('button,[role=button],svg').forEach(el => {
                let a = (el.getAttribute('aria-label')||'').toLowerCase();
                let t = (el.textContent||'').toLowerCase();
                if (a.includes('upload')||a.includes('file')||a.includes('clip')||a.includes('attach')||
                    t.includes('上传')||t.includes('文件')||t.includes('附件')){
                    r.uploadButtons.push({
                        tag: el.tagName,
                        aria: a.substring(0,40),
                        text: t.substring(0,40),
                        class: el.className?.toString()?.substring(0,60)||''
                    });
                }
            });
            if (r.fileInputs.length > 0) r.method = 'hidden_input';
            return r;
        }""")
        if upload_info:
            profile.data["_upload_diagnosis"] = upload_info
            profile.save()
            print(f"  [进化] {platform} 上传能力已记录: {upload_info.get('method','unknown')}")
    except Exception as e:
        print(f"  [进化] 上传诊断失败: {e}")


def check_platform_config():
    """首次使用检查：打印各平台配置状态。"""
    from common import get_session_url, is_valid_session_url
    lines = []
    sp = get_search_platform()
    kp = get_synthesis_platform()
    for plat in list(dict.fromkeys([sp, kp])):  # 去重
        url = get_session_url(platform=plat)
        ok = is_valid_session_url(url, plat)
        status = "✓ 已配置" if ok else "✗ 未配置（首页）"
        lines.append(f"  {plat}: {status}  {url[:80]}")
    lines.append(f"  搜索={sp}  整合={kp}  搜索失败=拒绝  总结失败=降级本地API")
    return "\n".join(lines)


def _submit_to_platform(platform, page, prompt, topic):
    plat = load_platform_module(platform)
    if not plat: return False, f"平台 {platform} 未定型"
    try:
        remaining, ok = submit_and_verify(plat, page, prompt)
        if ok:
            return True, None
        # 输入框未清空：重试一次（带回验证）
        plat.submit(page); time.sleep(1.5)
        remaining2, ok2 = submit_and_verify(plat, page, prompt)
        if ok2:
            return True, None
        return False, f"重试后仍失败(remaining={remaining2})"
    except Exception as e:
        return False, str(e)


def _send_one(browser, platform, question, config, depth, decomposed, stage="search"):
    """发送一个问题，返回 (page, prompt, topic) 或 (None,None,None)。

    stage="search" 用 build_search_prompt（开放性探索+可靠性自标注）
    stage="synthesis" 用 build_synthesis_prompt（被动验证+可信度报告）
    """
    print(f"  [_send_one] 开始 | stage={stage} | q_len={len(question)}",
          file=sys.stderr, flush=True)
    if stage == "synthesis":
        topic, prompt = build_synthesis_prompt(question, depth)
    else:
        topic, prompt = build_search_prompt(question, depth)
    print(f"  [_send_one] prompt 构建完成 | topic={topic[:40]} | prompt_len={len(prompt)}",
          file=sys.stderr, flush=True)
    ok, errors = validate_prompt(prompt, topic)
    if not ok:
        print(f"  [{platform}] prompt 验证失败")
        return None, None, None

    session_url = get_session_url(platform=platform)

    if not is_valid_session_url(session_url, platform):
        print(f"  [{platform}] ⚠ 未配置聊天链接（当前是首页，无法使用）")
        print(f"  [{platform}] → 请先在浏览器中打开 {platform} 创建一个新聊天")
        print(f"  [{platform}] → 然后把聊天 URL 贴到 ConfigPage「会话链接」→ {platform}")
        raise ConfigError(f"平台 {platform} 未配置有效会话链接")

    try:
        page = ensure_page(browser, session_url, new_tab=False)
        time.sleep(2)
    except Exception as e:
        print(f"  [{platform}] 打开页面失败: {e}")
        return None, None, None
    print(f"  [_send_one] 页面就绪 | url={page.url[:60]}",
          file=sys.stderr, flush=True)

    ok_send, err = _submit_to_platform(platform, page, prompt, topic)
    if not ok_send:
        print(f"  [{platform}] 发送失败 ({str(err)[:40]})，重试...")
        try:
            page = ensure_page(browser, session_url, new_tab=False); time.sleep(2)
        except Exception as e:
            print(f"  [{platform}] 页面重连异常: {e}", file=sys.stderr)
        ok_send, err = _submit_to_platform(platform, page, prompt, topic)
        if not ok_send:
            print(f"  [{platform}] 重试失败: {err}")
            return None, None, None

    time.sleep(0.5)
    if page.url != session_url:
        from common import _save_session
        _save_session(platform, page.url)
        print(f"  [{platform}] ✓ 已发送 (新会话)")
    else:
        print(f"  [{platform}] ✓ 已发送")
    return page, prompt, topic


def _wait_one(platform, page, prompt, topic, max_wait=180, on_event=None, stage_name="search"):
    """轮询等待提取。DOM 提取为主（避免 textContent 漏标记），结尾标记判定完成。

    on_event: 可选回调，用于推送 stage_progress 事件到 C++ 前端。
    textContent 兜底路径接入自进化：提取失败时触发 FailureAnalyzer → StrategyAdapter。
    """
    from evolution import load_or_create_polling, load_or_create_profile
    from evolution import FailureAnalyzer, StrategyAdapter
    from extractor import dom_extract

    polling = load_or_create_polling(platform)
    profile = load_or_create_profile(platform)
    interval = polling.get_interval()
    stability_rounds = polling.get_stability_rounds()
    deadline = time.time() + max_wait
    last_len, stable_count = 0, 0
    start_ts = time.time()
    no_closing_stable = 0
    closing_marker = f"[搜索主题：{topic}]"
    last_progress_time = 0  # 进度推送节流

    print(f"  [轮询] 间隔={interval}s 稳定阈值={stability_rounds}轮 (DOM模式)")

    while time.time() < deadline:
        time.sleep(interval)

        # 检查 C++ 取消信号
        if cancel_flag.is_set():
            print(f"  [轮询] 收到取消信号，中止等待 ({platform})")
            return None

        elapsed = int(time.time() - start_ts)

        # 主导：DOM 直接提取最后一个 AI 回复
        content = dom_extract(page, platform)

        # 动态进度推送（节流：每 3s 最多推一次）
        if on_event and time.time() - last_progress_time >= 3.0:
            last_progress_time = time.time()
            on_event("stage_progress", stage=stage_name, platform=platform,
                     elapsed_sec=elapsed,
                     content_len=len(content) if content else 0)

        if content and is_content_complete(content, platform=platform):
            tail = content.strip()[-300:]
            has_closing = closing_marker in tail

            if has_closing:
                no_closing_stable = 0
                print(f"  [轮询] {elapsed}s | DOM={len(content)}字符 | "
                      f"结尾标记✓ 稳定{stable_count}/{stability_rounds}")
                if len(content) == last_len:
                    stable_count += 1
                    if stable_count >= stability_rounds:
                        polling.adapt_interval()
                        polling.update_closing_marker_reliability(True)
                        return content
                else:
                    stable_count = 0
                last_len = len(content)
            else:
                stable_count = 0
                # 内容稳定但无结尾标记：AI 可能已完成但忘记放标记
                if len(content) == last_len and len(content) > 500:
                    no_closing_stable += 1
                else:
                    no_closing_stable = 0
                last_len = len(content)
                if elapsed % 15 == 0:
                    print(f"  [轮询] {elapsed}s | DOM={len(content)}字符 | "
                          f"等待结尾标记...(稳定{no_closing_stable}轮)")
                # 无标记但内容长期稳定（10轮≈30s）：AI 已完成，直接提取
                closing_threshold = polling.get_no_closing_threshold()
                if no_closing_stable >= closing_threshold:
                    polling.adapt_interval()
                    # 自进化：该平台不靠谱放结尾标记，下次降低阈值
                    polling.update_closing_marker_reliability(False)
                    print(f"  [兜底] {elapsed}s | 无结尾标记但内容稳定{no_closing_stable}轮 ({len(content)}字符)")
                    return content
        else:
            stable_count = 0
            last_len = 0
            # 辅助：textContent 标记提取
            raw = safe_page_text(page)
            if raw:
                txt_content, diagnosis = extract_with_diagnosis(raw, prompt, topic, platform)
                # 自进化：诊断 → 适配
                if diagnosis and diagnosis.get("adaptable"):
                    try:
                        analysis = FailureAnalyzer.analyze(raw, txt_content, platform, profile)
                        if analysis and analysis.get("adaptable"):
                            StrategyAdapter.adapt(profile, analysis)
                    except Exception as e:
                        print(f"  [自适应] FailureAnalyzer 异常: {e}", file=sys.stderr)
                if txt_content and is_content_complete(txt_content, platform=platform):
                    tail = txt_content.strip()[-300:]
                    if closing_marker in tail:
                        polling.adapt_interval()
                        print(f"  [textContent] {elapsed}s | {len(txt_content)}字符")
                        return txt_content

    # 超时兜底：DOM → textContent → 放弃
    content = dom_extract(page, platform)
    if content and len(content) > 150:
        print(f"  [DOM兜底] ({len(content)}字符)")
        return content
    raw = safe_page_text(page)
    if raw:
        content, diagnosis = extract_with_diagnosis(raw, prompt, topic, platform)
        # 自进化：超时兜底说明策略需要调整
        if diagnosis and diagnosis.get("adaptable"):
            try:
                analysis = FailureAnalyzer.analyze(raw, content, platform, profile)
                if analysis and analysis.get("actionable"):
                    StrategyAdapter.adapt(profile, analysis)
            except Exception as e:
                print(f"  [自适应] 超时诊断异常: {e}", file=sys.stderr)
        if content and len(content) > 150:
            return content
    return None


def _diagnose_and_adapt(error_type, platform=None):
    """故障诊断 + 自进化联动。

    1. 运行诊断检查，定位根因
    2. 如果诊断发现平台相关问题，触发 StrategyAdapter 适配策略
    3. 输出人类可读诊断结果
    """
    ctx = {"platform": platform} if platform else {}
    result = diagnose(error_type, ctx)
    print(f"\n{format_diagnosis(result)}")

    # 诊断 → 进化联动：平台脚本问题/配置问题 → 触发策略适配
    if result["failed"] > 0:
        try:
            from evolution import load_or_create_profile, StrategyAdapter
            ep = load_or_create_profile(platform) if platform else load_or_create_profile("deepseek")
            # 将诊断失败项转为进化信号
            fail_msgs = [r["message"] for r in result["results"] if not r["passed"]]
            diagnosis = {
                "failure_type": error_type,
                "evidence": "; ".join(fail_msgs[:3]),
                "severity": "high" if result["failed"] >= 2 else "medium",
                "suggestion": result["diagnosis"],
                "adaptable": True,
                "scope": "platform_specific" if platform else "cross_platform",
            }
            adapted = StrategyAdapter.adapt(ep, diagnosis)
            if adapted:
                print(f"  [自适应] 策略已适配: {adapted.get('changes', [])}")
        except Exception as e:
            print(f"  [自适应] _diagnose_and_adapt 异常: {e}", file=sys.stderr)

    return result


def execute(plan_dict, progress_callback=None, on_event=None,
            config=None):
    """执行搜索计划。

    L2: 单平台搜索 + 可靠性评估（无重搜）
    L3: 搜索（可靠性循环）+ 整合验证（被动验证模式）

    on_event: 可选回调 on_event(event_type, **payload)，用于推送 JSON 事件到 stdout。

    config: Protocol 模式必须显式注入。
        仅 CLI 和测试回退到内部 load_config()。
    """
    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        if on_event:
            on_event("error", error_type="import_failed", platform="system",
                     detail="未安装 playwright")
        return {"error": "未安装 playwright"}

    if config is None:
        from common import load_config as _lc
        config = _lc()

    depth = plan_dict.get("depth", "L2")
    sqs = plan_dict["sub_questions"]
    results = []
    all_links = []
    sp = config.get("search_platform", "deepseek")
    kp = config.get("synthesis_platform", "deepseek")
    send_failures = 0
    MAX_SEND_FAILURES = 3
    replan = plan_dict.get("replan_triggers", {})
    max_research_rounds = replan.get("max_research_rounds", 2)

    with sync_playwright() as p:
        print(f"[Execute] sync_playwright 就绪 | depth={depth} | cdp={config.get('cdp_port', DEFAULT_CDP_PORT)}",
              file=sys.stderr)
        browser, _ = ensure_browser(p, config.get("cdp_port", DEFAULT_CDP_PORT),
                                   launch_policy="external")
        print(f"[Execute] 浏览器已连接", file=sys.stderr)

        if depth == "L2":
            # === L2: 单平台搜索 + 可靠性评估 ===
            sq = sqs[0]
            stage = sq.get("stage", "search")
            print(f"\n[Send] L2 {sp}: {sq['question'][:50]}")
            if on_event:
                on_event("stage_start", stage="search_1", question=sq["question"], platform=sp)
            try:
                page, prompt, topic = _send_one(browser, sp, sq["question"],
                                                config, depth, True, stage)
            except ConfigError as ce:
                print(f"  ⛔ 配置错误: {ce}")
                if on_event:
                    on_event("error", error_type="config_error", platform=sp, detail=str(ce))
                return {
                    "results": [],
                    "error": str(ce),
                    "links": [], "all_sources": [],
                    "original_query": plan_dict.get("original_query", ""),
                }
            if page:
                send_failures = 0
                print(f"  [等待] 提取中...")
                t_start = time.time()
                content = _wait_one(sp, page, prompt, topic, max_wait=180,
                                    on_event=on_event, stage_name="search_1")
                elapsed = time.time() - t_start
                if content:
                    # 内容质量验证（对齐 WebAISearch _validate_extraction）
                    valid, vreason = validate_content(content, topic, sp)
                    if not valid:
                        print(f"  [{sp}] ⚠ 内容质量不合格: {vreason}")
                        results.append({"question": sq["question"], "platform": sp,
                                        "content": None, "error": f"内容质量不合格: {vreason}",
                                        "gaps": [], "links": []})
                        if on_event:
                            on_event("stage_done", stage="search_1", platform=sp,
                                     content_len=0, status="invalid_content", error=vreason)
                    else:
                        reliability = assess_reliability(content)
                        gaps = detect_gaps(content)
                        links = extract_links(content)
                        all_links.extend(links)
                        results.append({"question": sq["question"], "platform": sp,
                            "content": content, "gaps": gaps, "links": links,
                            "content_len": len(content), "reliability": reliability})
                        log_entry("certus", "output", f"[{sp}] {len(content)}字符 可靠={reliability.get('reliable')}")
                        print(f"  [{sp}] ✓ {len(content)}字符 可靠={reliability.get('reliable')}")
                        if on_event:
                            source_analysis = analyze_sources(links)
                            named = extract_named_sources(content)
                            all_sources = source_analysis.get("sources", []) + named
                            on_event("stage_done", stage="search_1", platform=sp,
                                     content_len=len(content), reliability=reliability,
                                     elapsed_sec=round(elapsed, 1),
                                     sources=all_sources)
                else:
                    results.append({"question": sq["question"], "platform": sp,
                                    "content": None, "error": "提取超时", "gaps": [], "links": []})
                    if on_event:
                        on_event("stage_done", stage="search_1", platform=sp,
                                 content_len=0, status="timeout", error="提取超时")
            else:
                send_failures += 1
                if send_failures >= MAX_SEND_FAILURES:
                    print(f"  ⛔ 连续 {send_failures} 次发送失败，请手动检查浏览器状态后重试")
                    _diagnose_and_adapt("send_failed", platform=sp)
                    if on_event:
                        on_event("error", error_type="send_failed", platform=sp,
                                 detail=f"连续{send_failures}次发送失败")
        else:
            # === L3: 搜索（可靠性循环）+ 整合验证 ===
            search_sqs = [s for s in sqs if s.get("stage") != "synthesis"]
            synth_sqs = [s for s in sqs if s.get("stage") == "synthesis"]
            print(f"[Execute] L3 分解: 搜索={len(search_sqs)}个 整合={len(synth_sqs)}个",
                  file=sys.stderr, flush=True)

            # --- 阶段 1: 搜索 + 可靠性循环 ---
            for i, sq in enumerate(search_sqs):
                if cancel_flag.is_set():
                    print("  [取消] 搜索已取消，跳过剩余问题")
                    break

                stage_name = f"search_{i+1}"
                print(f"\n[Search] L3 [{i+1}/{len(search_sqs)}] {sp}: {sq['question'][:60]}",
                      file=sys.stderr, flush=True)
                if on_event:
                    on_event("stage_start", stage=stage_name, question=sq["question"], platform=sp)
                content = None
                reliability = None
                re_search_count = 0

                for round_num in range(max_research_rounds + 1):
                    if cancel_flag.is_set():
                        print("  [取消] 搜索已取消，中止可靠性循环")
                        break
                    if round_num > 0:
                        print(f"  [重搜] 第{round_num}轮 ({sq['question'][:40]}...)")
                    print(f"  [Search] _send_one 开始 | round={round_num}",
                          file=sys.stderr, flush=True)
                    try:
                        page, prompt, topic = _send_one(browser, sp, sq["question"],
                                                        config, depth, True, "search")
                    except ConfigError as ce:
                        print(f"  ⛔ 配置错误: {ce}")
                        if on_event:
                            on_event("error", error_type="config_error", platform=sp,
                                     detail=str(ce))
                        # 配置错误不可恢复，直接终止整个搜索
                        raise
                    if not page:
                        send_failures += 1
                        if send_failures >= MAX_SEND_FAILURES:
                            print(f"  ⛔ 连续 {send_failures} 次发送失败，请手动检查浏览器状态后重试")
                            _diagnose_and_adapt("send_failed", platform=sp)
                            if on_event:
                                on_event("error", error_type="send_failed", platform=sp,
                                         detail=f"连续{send_failures}次发送失败")
                            break
                        continue

                    send_failures = 0
                    print(f"  [等待] 提取中...")
                    t_start = time.time()
                    content = _wait_one(sp, page, prompt, topic, max_wait=180,
                                        on_event=on_event,
                                        stage_name=f"search_{i+1}")
                    elapsed = time.time() - t_start
                    if not content:
                        if cancel_flag.is_set():
                            print("  [取消] 提取已取消")
                        break

                    reliability = assess_reliability(content)
                    status = "兜底通过" if reliability.get("fallback") else \
                             f"已确认={reliability['confirmed']} 推断={reliability['inferred']} 未确认={reliability['unconfirmed']}"
                    print(f"  [可靠性] {status} → {'✓ 可靠' if reliability['reliable'] else '✗ 不可靠'}")

                    # 自进化：不可靠 → 适配提取策略
                    if not reliability["reliable"] and not reliability.get("fallback"):
                        try:
                            from evolution import load_or_create_profile, StrategyAdapter
                            ep = load_or_create_profile(sp)
                            StrategyAdapter.adapt(ep, {
                                "failure_type": "low_reliability",
                                "evidence": f"confirmed={reliability['confirmed']} inferred={reliability['inferred']} unconfirmed={reliability['unconfirmed']}",
                                "severity": "medium",
                                "suggestion": "提示词中加强可靠性要求，或降低该平台标记依赖",
                                "adaptable": True,
                                "scope": "platform_specific",
                            })
                        except Exception as e:
                            print(f"  [自适应] 可靠性适配异常: {e}", file=sys.stderr)

                    if reliability["reliable"]:
                        break

                    re_search_count += 1

                if content:
                    # 内容质量验证（对齐 WebAISearch _validate_extraction）
                    # 注意：topic 来自 _send_one，包含 @hash（与 AI 回复中的标记一致）
                    valid, vreason = validate_content(content, topic or "", sp)
                    if not valid:
                        print(f"  [{sp}] ⚠ 内容质量不合格: {vreason}")
                        results.append({"question": sq["question"], "platform": sp,
                                        "content": None, "error": f"内容质量不合格: {vreason}",
                                        "gaps": [], "links": []})
                        if on_event:
                            on_event("stage_done", stage=stage_name, platform=sp,
                                     content_len=0, status="invalid_content", error=vreason)
                    else:
                        gaps = detect_gaps(content)
                        links = extract_links(content)
                        all_links.extend(links)
                        results.append({"question": sq["question"], "platform": sp,
                            "content": content, "gaps": gaps, "links": links,
                            "content_len": len(content),
                            "reliability": reliability or {},
                            "re_search_count": re_search_count})
                        log_entry("certus", "output", f"[{sp}] {len(content)}字符 重搜={re_search_count}次")
                        print(f"  [{sp}] ✓ {len(content)}字符 (重搜{re_search_count}次)")
                        if on_event:
                            source_analysis = analyze_sources(links)
                            named = extract_named_sources(content)
                            on_event("stage_done", stage=stage_name, platform=sp,
                                     content_len=len(content), reliability=reliability or {},
                                     elapsed_sec=round(elapsed, 1),
                                     re_search_count=re_search_count,
                                     sources=source_analysis.get("sources", []) + named)
                else:
                    results.append({"question": sq["question"], "platform": sp,
                                    "content": None, "error": "提取超时", "gaps": [], "links": []})
                    print(f"  [{sp}] ✗ 提取超时")
                    if on_event:
                        on_event("stage_done", stage=stage_name, platform=sp,
                                 content_len=0, status="timeout", error="提取超时")

            # --- 阶段 2: 整合验证 ---
            if synth_sqs:
                sq = synth_sqs[0]
                if on_event:
                    on_event("stage_start", stage="synthesis", question=sq["question"], platform=kp)
                # 1. 采集素材写入临时文件
                materials = "\n\n---\n\n".join([
                    f"## 采集方向 {i+1}\n{r['content']}"
                    for i, r in enumerate(results) if r.get("content")
                ])
                data_dir = (config or {}).get("data_dir") or os.path.join(os.path.dirname(SCRIPT_DIR), "data")
                os.makedirs(data_dir, exist_ok=True)
                mat_file = os.path.join(data_dir, "_materials.md")
                try: os.remove(mat_file)
                except Exception: pass
                with open(mat_file, "w", encoding="utf-8") as f:
                    f.write(materials)
                print(f"  [素材] {len(materials)}字符 → {mat_file}")

                synthesis_ok = True
                if len(materials) < 100:
                    print(f"  [{kp}] ⚠ 素材文件内容不足，跳过整合")
                    try: os.remove(mat_file)
                    except Exception: pass
                    results.append({"question": sq["question"], "platform": kp,
                                    "content": None, "error": "素材不足", "gaps": [], "links": []})
                    synthesis_ok = False

                # 2. 打开整合平台 + 上传素材 + 发送验证提示词
                if synthesis_ok:
                    session_url = get_session_url(platform=kp)
                    if not is_valid_session_url(session_url, kp):
                        print(f"  [{kp}] ⚠ 未配置聊天链接 → 降级本地 API 总结")
                        content = _synthesize_local(materials, plan_dict.get("original_query", ""))
                        if content:
                            results.append({"question": sq["question"], "platform": f"{kp}(本地)",
                                "content": content, "gaps": [], "links": [],
                                "content_len": len(content)})
                            log_entry("certus", "output", f"[{kp}/本地] {len(content)}字符")
                            print(f"  [{kp}/本地] ✓ {len(content)}字符")
                        else:
                            results.append({"question": sq["question"], "platform": kp,
                                            "content": None, "error": "本地总结也失败", "gaps": [], "links": []})
                        synthesis_ok = False

                if synthesis_ok:
                    try:
                        page = ensure_page(browser, session_url, new_tab=False)
                        time.sleep(2)
                    except Exception as e:
                        print(f"  [{kp}] 打开页面失败: {e}")
                        results.append({"question": sq["question"], "platform": kp,
                                        "content": None, "error": f"页面: {e}", "gaps": [], "links": []})
                        synthesis_ok = False

                if synthesis_ok:
                    # 生成合成阶段提示词
                    topic, prompt = build_synthesis_prompt(sq["question"], depth)
                    if not validate_prompt(prompt, topic)[0]:
                        print(f"  [{kp}] prompt 验证失败")
                        results.append({"question": sq["question"], "platform": kp,
                                        "content": None, "error": "prompt验证", "gaps": [], "links": []})
                        synthesis_ok = False

                if synthesis_ok:
                    # 上传素材 + 发送验证提示词
                    print(f"\n[Verify] L3 {kp}: {sq['question'][:60]}")
                    plat_mod = load_platform_module(kp)
                    uploaded = False
                    if plat_mod and hasattr(plat_mod, "upload_file"):
                        try:
                            uploaded = plat_mod.upload_file(page, mat_file)
                            print(f"  [上传] {'✓' if uploaded else '✗'}")
                            if not uploaded:
                                _evolve_upload(kp, page)
                        except Exception as e:
                            print(f"  [上传] 异常: {e}")
                            _evolve_upload(kp, page)

                    ok_send, err = _submit_to_platform(kp, page, prompt, topic)
                    if not ok_send:
                        print(f"  [{kp}] 发送失败 ({str(err)[:40]})，重试...")
                        try:
                            page = ensure_page(browser, session_url, new_tab=False); time.sleep(2)
                            if not uploaded:
                                plat_mod2 = load_platform_module(kp)
                                if plat_mod2 and hasattr(plat_mod2, "upload_file"):
                                    plat_mod2.upload_file(page, mat_file)
                        except Exception as e:
                            print(f"  [{kp}] 文件上传异常: {e}", file=sys.stderr)
                        ok_send, err = _submit_to_platform(kp, page, prompt, topic)
                        if not ok_send:
                            print(f"  [{kp}] 重试失败: {err}")
                            results.append({"question": sq["question"], "platform": kp,
                                            "content": None, "error": f"发送: {err}", "gaps": [], "links": []})
                            synthesis_ok = False

                if synthesis_ok:
                    time.sleep(0.5)
                    if page.url != session_url:
                        from common import _save_session
                        _save_session(platform, page.url)
                        print(f"  [{kp}] ✓ 已发送 (新会话)")
                    else:
                        print(f"  [{kp}] ✓ 已发送")
                    try: os.remove(mat_file)
                    except Exception: pass

                    print(f"  [等待] {kp} 验证整合中...")
                    content = _wait_one(kp, page, prompt, topic, max_wait=300,
                                        on_event=on_event, stage_name="synthesis")
                    if content:
                        valid, reason = _is_valid_synthesis(content)
                        if not valid:
                            print(f"  [{kp}] LLM判定无效 ({reason}) → 降级本地 API 总结")
                            local = _synthesize_local(materials, plan_dict.get("original_query", ""))
                            if local:
                                results.append({"question": sq["question"], "platform": f"{kp}(本地)",
                                    "content": local, "gaps": [], "links": [],
                                    "content_len": len(local)})
                                log_entry("certus", "output", f"[{kp}/本地] {len(local)}字符")
                                print(f"  [{kp}/本地] ✓ {len(local)}字符")
                            else:
                                results.append({"question": sq["question"], "platform": kp,
                                                "content": content, "error": "整合无效且本地总结失败",
                                                "gaps": [], "links": []})
                        else:
                            gaps = detect_gaps(content)
                            links = extract_links(content)
                            all_links.extend(links)
                            results.append({"question": sq["question"], "platform": kp,
                                "content": content, "gaps": gaps, "links": links,
                                "content_len": len(content)})
                            log_entry("certus", "output", f"[{kp}] {len(content)}字符")
                            print(f"  [{kp}] ✓ {len(content)}字符")
                    else:
                        results.append({"question": sq["question"], "platform": kp,
                                        "content": None, "error": "超时", "gaps": [], "links": []})

                # 整合阶段完成事件
                if on_event and synth_sqs:
                    synth_result = None
                    for r in reversed(results):
                        if r.get("question") == synth_sqs[0]["question"]:
                            synth_result = r
                            break
                    if synth_result:
                        if synth_result.get("content"):
                            on_event("stage_done", stage="synthesis", platform=kp,
                                     content_len=synth_result.get("content_len", 0))
                        else:
                            on_event("stage_done", stage="synthesis", platform=kp,
                                     content_len=0, status="failed",
                                     error=synth_result.get("error", "未知错误"))

    # 清理临时材料文件
    try:
        cleanup_dir = (config or {}).get("data_dir") or os.path.join(os.path.dirname(SCRIPT_DIR), "data")
        mat_file = os.path.join(cleanup_dir, "_materials.md")
        if os.path.exists(mat_file):
            os.remove(mat_file)
    except Exception as e:
        print(f"  [final_review] 临时文件清理异常: {e}", file=sys.stderr)

    return {
        "results": results,
        "gaps_total": sum(len(r.get("gaps", [])) for r in results),
        "all_links": list(set(all_links)),
    }


def validate_content(content, topic="", platform=""):
    """提取后内容质量验证（对齐 WebAISearch main.py:_validate_extraction）。

    返回 (ok: bool, reason: str)。
    """
    if not content or len(content) < 150:
        return False, f"内容过短 ({len(content) if content else 0} 字符)"
    # 检查结尾标记至少出现 2 次（形成标记对）
    marker = f"[搜索主题：{topic}]"
    marker_count = content.count(marker) if topic else 2
    if marker_count < 2:
        print(f"  [validate] 标记匹配失败 | marker='{marker[:50]}' | "
              f"count={marker_count} | content_tail={content[-100:]!r}",
              file=sys.stderr, flush=True)
        return False, f"标记不足 (仅 {marker_count} 个)"
    # 排除明显错误：提取到了 prompt 而非 AI 回复
    bad_starts = ["请联网搜索", "请搜索", "ERROR", "搜索主题"]
    stripped = content.strip()
    if any(stripped.startswith(s) for s in bad_starts):
        return False, "内容以搜索指令开头（可能提取了 prompt 而非回复）"
    # 排除明显的平台错误页面
    bad_patterns = ["Something went wrong", "请登录", "Please log in", "Access denied"]
    for bp in bad_patterns:
        if bp.lower() in stripped[:500].lower():
            return False, f"页面包含错误信息: {bp}"
    return True, f"{len(content)} 字符, 标记×{marker_count}"


def final_review(results, user_query):
    """LLM API 最终审阅：对搜索结果做审查、整理、总结。

    在所有平台内容提取完成后，调用 DeepSeek API 对原始素材进行最终编辑审阅，
    生成一份逻辑一致、格式统一、显式标注不确定信息的中文研究报告。

    返回: (reviewed_report: str, meta: dict)
    - 无 API key 时返回 (None, {"reviewed": False, "reason": "..."})
    """
    from common import call_deepseek_api

    # 组装各平台素材（带可靠性标注）
    parts = []
    for i, r in enumerate(results):
        if not r.get("content"):
            continue
        plat = r.get("platform", "unknown")
        content = r["content"]
        reliability = r.get("reliability", {})
        gaps = r.get("gaps", [])

        header = f"## 来源 {i+1}: {plat}\n"
        header += f"内容长度: {len(content)} 字符"
        if reliability:
            header += f" | 已确认: {reliability.get('confirmed', 0)} 推断: {reliability.get('inferred', 0)} 未确认: {reliability.get('unconfirmed', 0)}"
        if gaps:
            header += f" | 信息缺口: {len(gaps)} 处"
        header += f"\n"
        parts.append(header + content[:10000])

    materials = "\n\n---\n\n".join(parts)

    if len(materials) < 100:
        return None, {"reviewed": False, "reason": "素材内容不足"}

    reviewed = call_deepseek_api(
        system_prompt=(
            "你是资深技术研究报告编辑。你的任务是对研究素材进行最终审阅整理。"
            "遵循原则：\n"
            "1. 保持原内容实质信息不变，不添加虚构信息\n"
            "2. 检查逻辑一致性，标注矛盾或不确定之处\n"
            "3. 统一格式为清晰的中文 Markdown 报告\n"
            "4. 推荐结构：概述 → 核心发现 → 方案对比（如有）→ 风险与不确定性 → 总结建议\n"
            "5. 对未确认的信息显式标注「待验证」\n"
            "6. 保留原始来源和引用信息"
        ),
        user_prompt=(
            f"原始问题：{user_query}\n\n"
            f"以下是从各平台采集的研究素材（已附可靠性标注）：\n\n{materials[:60000]}\n\n"
            "请对以上素材进行最终审阅整理，生成一份完整、清晰、可靠的中文研究报告。"
        ),
        model="deepseek-v4-pro",
        max_tokens=8192,
        temperature=0.3,
        timeout=180,
    )

    if reviewed is None:
        return None, {"reviewed": False, "reason": "API 调用失败"}
    return reviewed, {"reviewed": True}


def execute_simple(query, platform="deepseek", depth="L2", config=None):
    from prompt_builder import refine_query
    refined = refine_query(query)
    plan_dict = {
        "original_query": query, "depth": depth,
        "sub_questions": [{"question": refined, "platform": platform, "reason": "指定"}],
        "decomposed": False, "replan_triggers": {},
    }
    outcome = execute(plan_dict, config=config)
    if "error" in outcome: return f"ERROR: {outcome['error']}"
    r = outcome.get("results", [])
    return r[0].get("content") if r else None
