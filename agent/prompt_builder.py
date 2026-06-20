# -*- coding: utf-8 -*-
"""搜索 Prompt —— 搜索阶段开放式探索+可靠性自标注，整合阶段被动验证+可信度报告。"""
import os, sys, time, random, hashlib, string, re
from common import load_config

DEFAULT_API = "https://api.deepseek.com/v1"

# === 可靠性标注正则 ===
RELIABILITY_PATTERNS = {
    "confirmed": re.compile(r'\[已确认(?::\s*(.+?))?\]'),
    "inferred": re.compile(r'\[推断(?::\s*(.+?))?\]'),
    "unconfirmed": re.compile(r'\[未确认(?::\s*(.+?))?\]'),
}


def refine_query(user_query):
    """查询重构 —— 将用户原始输入优化为适合 AI 搜索的结构化问题。

    解决的问题：用户输入往往是需求文档/想法描述，而非搜索问题。
    例如：
      原始: "我想做一个XX工具，功能1...功能2...可行性分析关注..."
      优化: "XX工具的技术可行性分析：核心功能架构、关键难点（A/B/C）及现有解决方案"

    规则：
    - 短且清晰的输入（≤100字，无换行）直接返回，不做多余处理
    - 重构只改格式和结构，不改语义（不增删用户意图）
    """
    # 快速判断：短且干净的输入不需要重构
    stripped = user_query.strip()
    if len(stripped) <= 100 and '\n' not in stripped:
        return stripped

    try:
        from common import call_deepseek_api
        refined = call_deepseek_api(
            system_prompt=(
                "你是搜索查询优化器。将用户输入重构为适合 AI 深度搜索的问题。\n\n"
                "规则：\n"
                "1. 保留用户的所有意图和关注点，不增删内容\n"
                "2. 将需求描述/想法转化为明确的研究问题\n"
                "3. 多要点时用分号分隔，保持一段话（不超过 200 字）\n"
                "4. 去掉「我想」「请问」等口语，用客观陈述\n"
                "5. 确保输出可以直接作为搜索主题（AI 能理解要搜什么）\n"
                "6. 只输出优化后的查询，不要任何解释\n\n"
                "严格禁止：\n"
                "- 禁止改变用户的领域/主题（用户问小说就保持小说，问技术就保持技术）\n"
                "- 禁止添加用户没有提及的概念（如「哲学范式」「实证主义」）\n"
                "- 禁止将具体需求抽象为学术术语"
            ),
            user_prompt=stripped,
            model="deepseek-v4-pro",
            max_tokens=300,
            temperature=0.1,
            timeout=30,
        )
        if refined:
            result = refined.strip().strip('"').strip('*')
            if result and len(result) > 10:
                return result
    except Exception as e:
        print(f"[Refine] LLM 重构失败: {e}，使用原始输入")

    # 兜底：取第一行或前 200 字符
    first_line = stripped.split('\n')[0].strip()
    return first_line[:200] if first_line else stripped[:200]


def extract_intent(user_context):
    """提取搜索主题（≤40字），用于生成 topic 标记。"""
    # 含 traceback → 截取核心错误
    if 'Traceback (most recent call last):' in user_context:
        lines = [l.strip() for l in user_context.split('\n') if l.strip()]
        for line in reversed(lines):
            if 'Error' in line or 'Exception' in line:
                return line[:60]
        return lines[-1][:60] if lines else user_context[:60]

    try:
        from common import call_deepseek_api
        topic = call_deepseek_api(
            system_prompt="提取用户问题的核心搜索主题（≤40字）。只输出主题，不要解释。",
            user_prompt=user_context,
            model="deepseek-v4-pro",
            max_tokens=80,
            temperature=0.1,
            timeout=30,
        )
        return topic.strip() if topic else user_context[:80]
    except Exception:
        lines = [l for l in user_context.split("\n") if l.strip()]
        return lines[-1][:80] if lines else user_context[:80]


def _make_topic_marker(topic):
    """生成带 hash 的 topic 和标记字符串。"""
    topic_hash = hashlib.md5(str(time.time_ns()).encode()).hexdigest()[:4]
    topic = f"{topic}@{topic_hash}"
    marker = f"[搜索主题：{topic}]"
    return topic, marker


def _marker_fence(marker):
    """标记对定位要求（搜索和整合共享）。"""
    return (
        f"【定位要求】在你的回复最开头（第1行）和最末尾（最后1行）各放一行标记（共2行）：\n"
        f"`{marker}`\n"
        f"两行标记之间是你的正式回复内容。标记必须独占一行。"
    )


def build_search_prompt(topic, depth="L2"):
    """搜索阶段提示词：开放式探索 + 可靠性自标注。

    行为约束（不约束结构）：
    - 每条关键结论标注可信度：[已确认:url] / [推断] / [未确认]
    - 至少给出 2 条独立分析路径，每条路径有各自的结论和来源
    - 无法确认的结论诚实标注而非编造
    - 优先使用高可信度信源（官方文档、学术论文、开源仓库）
    """
    # 标记必须用短主题（AI 才会在回复中 echo 回来）
    original_topic = topic
    if len(topic) > 80:
        short_topic = extract_intent(topic)
    else:
        short_topic = topic
    topic_with_hash, marker = _make_topic_marker(short_topic)
    core = topic_with_hash.split("@")[0]

    # 原始查询比 core 丰富时，附加上下文（让 AI 知道完整需求）
    detail_block = ""
    if len(original_topic) > len(core) + 40:
        detail_block = f"\n\n---\n补充上下文（完整需求）：\n{original_topic[:500]}"

    prompt = (
        f"请联网搜索并深入分析以下问题，自行设计搜索策略和回复结构：\n\n"
        f"**{core}**\n"
        f"{detail_block}\n"
        f"---\n"
        f"【可靠性要求】\n"
        f"1. 每条关键结论必须标注可信度——**必须附带具体URL**（禁止只写名称）：\n"
        f"   - 有明确来源支撑 → [已确认: https://具体URL]\n"
        f"   - 基于推理但来源不直接 → [推断]\n"
        f"   - 无法找到可靠来源 → [未确认]\n"
        f"2. 至少给出 2 条独立分析路径（独立 = 不同角度/不同信源群）\n"
        f"3. 优先引用官方文档、学术论文、开源仓库，避免自媒体/营销号\n"
        f"4. 无法确认的结论诚实标注，不要为凑来源而引用低质信源\n"
        f"5. **重要**：所有引用来源统一放在回复末尾的「参考来源」段落，格式为：\n"
        f"   - [标题](URL) —— 便于系统提取和评分\n\n"
        f"{_marker_fence(marker)}"
    )
    return topic_with_hash, prompt


def build_synthesis_prompt(topic, depth="L2"):
    """整合阶段提示词：被动验证 + 交叉确认 + 可信度报告。

    行为约束（不约束结构）：
    - 对素材中每条关键结论，独立搜索至少一次进行确认
    - 标注验证结果：[已验证:url] / [存疑:原因] / [矛盾:来源A vs 来源B]
    - 生成带可信度评分的最终报告
    - 无法验证的标注 [未确认]，不编造确认来源
    """
    original_topic = topic
    if len(topic) > 80:
        short_topic = extract_intent(topic)
    else:
        short_topic = topic
    topic_with_hash, marker = _make_topic_marker(short_topic)
    core = topic_with_hash.split("@")[0]

    detail_block = ""
    if len(original_topic) > len(core) + 40:
        detail_block = f"\n\n---\n补充上下文（完整需求）：\n{original_topic[:500]}"

    prompt = (
        f"你是一位研究审核员。请对以下素材进行独立验证并生成最终报告：\n\n"
        f"**{core}**\n"
        f"{detail_block}\n"
        f"---\n"
        f"【验证要求】\n"
        f"1. 提取素材中的每条关键结论，独立搜索至少一次进行确认\n"
        f"2. 标注验证结果：\n"
        f"   - 搜索确认一致 → [已验证: 确认来源URL]\n"
        f"   - 搜索后仍有疑虑 → [存疑: 具体原因]\n"
        f"   - 不同来源说法冲突 → [矛盾: 来源A vs 来源B]\n"
        f"   - 无法找到独立来源验证 → [未确认]\n"
        f"3. 在报告末尾给出整体可信度评分（1-10），附评分理由\n"
        f"4. 不编造验证来源——宁可标注 [未确认]，不给虚假确认\n"
        f"5. 最终报告应包含：概述、已验证结论、存疑结论、矛盾标记、可信度评分\n\n"
        f"{_marker_fence(marker)}"
    )
    return topic_with_hash, prompt


def build_final_prompt(topic, depth="L2"):
    """兼容旧接口：默认走搜索阶段提示词。整合阶段请用 build_synthesis_prompt。"""
    return build_search_prompt(topic, depth)


def validate_prompt(prompt, topic):
    """防呆：确保 prompt 包含标记对。返回 (ok, errors)。"""
    errors = []
    if not prompt or len(prompt) < 50:
        errors.append("PROMPT_TOO_SHORT")
    if not topic or "@" not in topic:
        errors.append("TOPIC_NO_HASH")
    else:
        hash_part = topic.split("@")[-1]
        if len(hash_part) != 4:
            errors.append(f"TOPIC_HASH_LEN:{len(hash_part)}")

    marker = f"[搜索主题：{topic}]"
    if marker not in prompt:
        errors.append("MARKER_MISSING")

    return len(errors) == 0, errors


def assess_reliability(content):
    """统计可靠性标注，判定搜索结果是否可靠。

    返回 {confirmed:N, inferred:N, unconfirmed:N, reliable:bool, fallback:bool}。

    兜底：0 个标注 → 默认通过（reliable=True, fallback=True），不阻塞流程。
    """
    if not content:
        return {"confirmed": 0, "inferred": 0, "unconfirmed": 0,
                "reliable": False, "fallback": True}

    counts = {
        "confirmed": len(RELIABILITY_PATTERNS["confirmed"].findall(content)),
        "inferred": len(RELIABILITY_PATTERNS["inferred"].findall(content)),
        "unconfirmed": len(RELIABILITY_PATTERNS["unconfirmed"].findall(content)),
    }
    total = sum(counts.values())

    if total == 0:
        counts["reliable"] = True
        counts["fallback"] = True
    else:
        counts["reliable"] = counts["confirmed"] > 0 and counts["confirmed"] >= counts["unconfirmed"]
        counts["fallback"] = False

    return counts


if __name__ == "__main__":
    import sys as _sys
    _sys.stdout.reconfigure(encoding='utf-8')
    ctx = _sys.argv[1] if len(_sys.argv) > 1 else _sys.stdin.read()
    topic = extract_intent(ctx)
    if not topic:
        print("NONE")
        _sys.exit(0)
    topic, prompt = build_final_prompt(topic)
    ok, errors = validate_prompt(prompt, topic)
    print(f"TOPIC:{topic}")
    print(f"VALID:{'OK' if ok else 'FAIL:' + '; '.join(errors)}")
    print(prompt)
