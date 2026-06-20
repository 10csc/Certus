# -*- coding: utf-8 -*-
"""核心逻辑单元测试 —— 不依赖浏览器，纯函数和纯数据测试。
运行方式: PYTHONUTF8=1 PYTHONIOENCODING=utf-8 python -m pytest test_core.py -v
"""

import os, sys, json, tempfile, shutil

# 确保 scripts 模块可 import
SCRIPT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scripts")
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

import pytest


# ============================================================
# Fixtures
# ============================================================

@pytest.fixture
def temp_skill_dir():
    """创建临时 SKILL_DIR 目录，内含 config.json。"""
    tmp = tempfile.mkdtemp(prefix="wais_test_")
    scripts_dir = os.path.join(tmp, "scripts")
    data_dir = os.path.join(tmp, "data")
    os.makedirs(scripts_dir, exist_ok=True)
    os.makedirs(data_dir, exist_ok=True)

    # Mock common 模块的 SKILL_DIR
    import common
    old_skill_dir = common.SKILL_DIR
    old_config_path = common.CONFIG_PATH
    common.SKILL_DIR = tmp
    common.CONFIG_PATH = os.path.join(tmp, "config.json")

    # 写默认 config
    cfg = {
        "version": 6,
        "platform_urls": {
            "deepseek": "https://chat.deepseek.com/a/chat/s/abc123",
        },
        "cdp_port": 9223,
        "project_name": "test_project",
        "deepseek_api": "https://api.deepseek.com/v1",
        "local_env": {"initialized": True},
    }
    with open(common.CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)

    yield tmp

    # teardown
    common.SKILL_DIR = old_skill_dir
    common.CONFIG_PATH = old_config_path
    shutil.rmtree(tmp, ignore_errors=True)


@pytest.fixture
def workspace_dir(temp_skill_dir):
    """利用 temp_skill_dir 初始化 workspace 相关路径。"""
    import workspace
    old_skill = workspace.SKILL_DIR
    old_ws = workspace.WORKSPACE_DIR
    old_mem = workspace.MEMORY_DIR
    workspace.SKILL_DIR = temp_skill_dir
    workspace.WORKSPACE_DIR = os.path.join(temp_skill_dir, "data", "workspace")
    workspace.MEMORY_DIR = os.path.join(temp_skill_dir, "data", "memory")
    os.makedirs(workspace.WORKSPACE_DIR, exist_ok=True)
    os.makedirs(workspace.MEMORY_DIR, exist_ok=True)
    yield
    workspace.SKILL_DIR = old_skill
    workspace.WORKSPACE_DIR = old_ws
    workspace.MEMORY_DIR = old_mem


# ============================================================
# common.py 测试
# ============================================================

class TestDetectPlatform:
    def test_deepseek(self, temp_skill_dir):
        import common
        assert common.detect_platform("https://chat.deepseek.com/") == "deepseek"
        assert common.detect_platform("https://chat.deepseek.com/a/chat/s/xxx") == "deepseek"

    def test_unknown(self, temp_skill_dir):
        import common
        assert common.detect_platform("https://www.baidu.com/") == "unknown"
        assert common.detect_platform("") == "unknown"


class TestIsValidSessionUrl:
    def test_valid_deepseek(self):
        import common
        assert common.is_valid_session_url(
            "https://chat.deepseek.com/a/chat/s/abc123", "deepseek")

    def test_invalid_homepage(self):
        import common
        assert not common.is_valid_session_url(
            "https://chat.deepseek.com/", "deepseek")

    def test_empty_url(self):
        import common
        assert not common.is_valid_session_url("", "deepseek")
        assert not common.is_valid_session_url(None, "deepseek")

    def test_unknown_platform(self):
        import common
        # 无 pattern 清单 → 走通用判断（path 非空即 valid）
        assert common.is_valid_session_url("https://example.com/some/path", "unknown")
        assert not common.is_valid_session_url("https://example.com/", "unknown")


class TestGetSessionUrl:
    def test_fixed_mode_session_found(self, temp_skill_dir):
        import common
        url = common.get_session_url(platform="deepseek")
        assert "/a/chat/s/abc123" in url

    def test_fixed_mode_returns_registered_url(self, temp_skill_dir):
        import common
        url = common.get_session_url(platform="deepseek")
        assert "chat.deepseek.com" in url

    def test_fixed_mode_no_project(self, temp_skill_dir):
        import common
        url = common.get_session_url(platform="deepseek")
        assert "/a/chat/s/abc123" in url

    def test_fallback_when_platform_none(self, temp_skill_dir):
        import common
        url = common.get_session_url(platform=None)
        assert url == common.DEFAULT_URL


class TestLoadConfig:
    def test_returns_dict(self, temp_skill_dir):
        import common
        cfg = common.load_config()
        assert isinstance(cfg, dict)
        assert cfg["version"] == 6

    def test_defaults_applied(self, temp_skill_dir):
        import common
        cfg = common.load_config()
        assert "cdp_port" in cfg
        assert "deepseek_api" in cfg


class TestGetOrCreateProject:
    def test_existing_project(self, temp_skill_dir):
        import common
        proj = common.get_or_create_project()
        assert proj == "certus"


# ============================================================
# prompt_builder.py 测试
# ============================================================

class TestExtractIntent:
    def test_traceback_input(self):
        tb = """Traceback (most recent call last):
  File "foo.py", line 10, in <module>
    main()
  File "foo.py", line 5, in main
    raise ValueError("Something went wrong")
ValueError: Something went wrong"""
        result = __import__("prompt_builder").extract_intent(tb)
        assert "ValueError" in result or "Something went wrong" in result

    def test_normal_input_no_llm(self):
        """LLM 不可用时应走 fallback（返回原始文本的前 80 字符）。"""
        result = __import__("prompt_builder").extract_intent("Python 3.13 asyncio 新特性")
        assert len(result) > 0
        assert "Python" in result or len(result) <= 80

    def test_empty_input(self):
        result = __import__("prompt_builder").extract_intent("")
        # 空字符串 fallback 到自身前 80 字符
        assert isinstance(result, str)


class TestBuildFinalPrompt:
    def test_basic_structure(self):
        topic, prompt = __import__("prompt_builder").build_final_prompt(
            "Python asyncio 选型", depth="L2"
        )
        assert "@" in topic
        assert len(topic.split("@")[-1]) == 4  # 4位hash
        assert "搜索主题" in prompt
        assert topic in prompt
        assert "标记" in prompt

    def test_l3_depth(self):
        topic, prompt = __import__("prompt_builder").build_final_prompt(
            "复杂问题分析", depth="L3"
        )
        assert "@" in topic
        assert "搜索主题" in prompt

    def test_chinese_topic(self):
        topic, prompt = __import__("prompt_builder").build_final_prompt(
            "为什么 DeepSeek 网页版比 API 慢？", depth="L2"
        )
        assert "为什么 DeepSeek" in topic
        assert "为什么 DeepSeek" in prompt

    def test_different_hash_per_call(self):
        import time
        t1, _ = __import__("prompt_builder").build_final_prompt("test")
        time.sleep(0.001)  # 确保时间戳不同
        t2, _ = __import__("prompt_builder").build_final_prompt("test")
        assert t1 != t2  # 每次生成不同 hash


class TestValidatePrompt:
    def test_valid(self):
        ok, errors = __import__("prompt_builder").validate_prompt(
            "[搜索主题：测试问题@abcd]\n这是一段足够长的搜索prompt内容用于联网搜索分析\n[搜索主题：测试问题@abcd]",
            "测试问题@abcd"
        )
        assert ok, f"errors: {errors}"
        assert len(errors) == 0

    def test_prompt_too_short(self):
        ok, errors = __import__("prompt_builder").validate_prompt(
            "短", "topic@hash"
        )
        assert not ok
        assert any("TOO_SHORT" in e for e in errors)

    def test_topic_no_hash(self):
        ok, errors = __import__("prompt_builder").validate_prompt(
            "这是一个足够长的 prompt 内容用于测试验证逻辑是否正常工作",
            "topic_without_hash"
        )
        assert not ok
        assert any("NO_HASH" in e for e in errors)

    def test_hash_length_wrong(self):
        ok, errors = __import__("prompt_builder").validate_prompt(
            "这是一个足够长的 prompt 内容用于测试验证逻辑是否正常工作",
            "topic@12345"  # 5位hash，应该是4位
        )
        assert not ok
        assert any("HASH_LEN" in e for e in errors)

    def test_marker_missing(self):
        ok, errors = __import__("prompt_builder").validate_prompt(
            "这是一个足够长的 prompt 内容但是没有包含搜索主题标记信息",
            "topic@abcd"
        )
        assert not ok
        assert any("MARKER_MISSING" in e for e in errors)


# ============================================================
# planner.py 测试
# ============================================================

class TestPlan:
    def test_l2_single_subquestion(self):
        plan = __import__("planner").plan("Python asyncio vs Trio", depth="L2")
        assert plan["depth"] == "L2"
        assert len(plan["sub_questions"]) == 1
        assert plan["decomposed"] == False

    def test_l3_decomposes(self):
        plan = __import__("planner").plan(
            "企业级 LLM 推理平台：推理框架选型、GPU调度、模型服务化",
            depth="L3"
        )
        assert plan["depth"] == "L3"
        assert len(plan["sub_questions"]) >= 1
        # 至少有一个搜索方向 + 可能有一个整合方向
        assert len(plan["sub_questions"]) <= 3  # 最多2个搜索 + 1个整合

    def test_plan_has_replan_triggers(self):
        plan = __import__("planner").plan("test query", depth="L2")
        triggers = plan["replan_triggers"]
        assert "timeout_no_new_info_sec" in triggers
        assert "credibility_below" in triggers
        assert "max_replan_rounds" in triggers
        assert triggers["max_replan_rounds"] <= 5

    def test_plan_has_original_query(self):
        plan = __import__("planner").plan("自定义搜索", depth="L2")
        assert plan["original_query"] == "自定义搜索"

    def test_project_context_injected(self):
        ctx = "使用 Triton+K8s, A100x8, 延迟<50ms"
        plan = __import__("planner").plan(
            "LLM 推理优化", depth="L2", project_context=ctx
        )
        assert plan["project_context"] == ctx

    def test_format_plan_output(self):
        plan = __import__("planner").plan("测试问题", depth="L2")
        formatted = __import__("planner").format_plan(plan)
        assert "测试问题" in formatted
        assert "L2" in formatted or "深度" in formatted
        assert "1个" in formatted or "子问题" in formatted


# ============================================================
# workspace.py 测试
# ============================================================

class TestWorkspaceState:
    def test_create_and_basic_lifecycle(self, workspace_dir):
        from workspace import WorkspaceState
        ws = WorkspaceState()
        assert ws.state["status"] == "created"

        ws.set_query("test query")
        assert ws.state["query"] == "test query"
        assert ws.state["status"] == "planned"

    def test_add_result(self, workspace_dir):
        from workspace import WorkspaceState
        ws = WorkspaceState()
        ws.add_result("deepseek", "测试问题", 3500, 0, 5)
        assert len(ws.state["results"]) == 1
        r = ws.state["results"][0]
        assert r["platform"] == "deepseek"
        assert r["content_len"] == 3500

    def test_checkpoint(self, workspace_dir):
        from workspace import WorkspaceState
        ws = WorkspaceState()
        ws.add_checkpoint("完成第一轮")
        assert len(ws.state["checkpoints"]) == 1
        assert "第一轮" in ws.state["checkpoints"][0]["label"]

    def test_increment_replan(self, workspace_dir):
        from workspace import WorkspaceState
        ws = WorkspaceState()
        assert ws.state["replan_count"] == 0
        ws.increment_replan()
        ws.increment_replan()
        assert ws.state["replan_count"] == 2

    def test_complete(self, workspace_dir):
        from workspace import WorkspaceState
        ws = WorkspaceState()
        ws.complete()
        assert ws.state["status"] == "completed"
        assert "completed_at" in ws.state

    def test_cleanup(self, workspace_dir):
        from workspace import WorkspaceState
        ws = WorkspaceState()
        ws.save()  # 先确保文件落盘
        path = ws.path
        assert os.path.exists(path)
        ws.cleanup()
        assert not os.path.exists(path)

    def test_get_summary(self, workspace_dir):
        from workspace import WorkspaceState
        ws = WorkspaceState()
        ws.set_query("测试查询")
        ws.add_result("deepseek", "问题A", 1000, 0, 3)
        ws.add_result("deepseek", "问题B", 2000, 1, 5)
        s = ws.get_summary()
        assert s["results"] == 2
        assert s["status"] == "executing"
        assert s["query"] == "测试查询"


class TestEpisodicMemory:
    def test_record_and_read(self, workspace_dir):
        from workspace import record_episode, read_episodes
        record_episode("testproj", "deepseek", "Python asyncio", "L2",
                       45, 8, 3500, 0)
        episodes = read_episodes("testproj")
        assert len(episodes) >= 1
        latest = episodes[-1]
        assert latest["platform"] == "deepseek"
        assert latest["depth"] == "L2"
        assert latest["credibility"] == 8

    def test_read_limit(self, workspace_dir):
        from workspace import record_episode, read_episodes
        for i in range(5):
            record_episode("testproj", "deepseek", f"topic_{i}", "L2",
                          30 + i, 7, 3000, 0)
        episodes = read_episodes("testproj", limit=3)
        assert len(episodes) == 3

    def test_read_empty(self, workspace_dir):
        from workspace import read_episodes
        episodes = read_episodes("nonexistent_project")
        assert episodes == []


class TestPlatformStats:
    def test_stats_calculation(self, workspace_dir):
        from workspace import record_episode, get_platform_stats
        record_episode("statsproj", "deepseek", "A", "L2", 50, 8, 4000, 0)
        record_episode("statsproj", "deepseek", "B", "L2", 30, 6, 3000, 2)
        record_episode("statsproj", "deepseek", "C", "L2", 40, 9, 5000, 0)

        stats = get_platform_stats("statsproj")
        assert "deepseek" in stats
        assert stats["deepseek"]["count"] == 3
        assert stats["deepseek"]["avg_credibility"] == pytest.approx(23.0/3.0, abs=0.1)
        assert stats["deepseek"]["avg_duration"] == 40.0


class TestSemanticMemory:
    def test_archive_and_search(self, workspace_dir):
        from workspace import archive_conclusion, search_semantic
        archive_conclusion("semproj", "Python asyncio 选型",
                          "推荐使用 asyncio，生态更成熟。",
                          sources=["https://example.com/1"])
        results = search_semantic("semproj", "asyncio")
        assert len(results) >= 1
        assert "推荐使用 asyncio" in results[0]

    def test_search_no_match(self, workspace_dir):
        from workspace import archive_conclusion, search_semantic
        archive_conclusion("semproj", "Docker 部署", "使用 docker-compose")
        results = search_semantic("semproj", "不存在的关键词_xyz123")
        assert results == []

    def test_search_empty_project(self, workspace_dir):
        from workspace import search_semantic
        results = search_semantic("never_existed_project", "anything")
        assert results == []


# ============================================================
# 运行入口
# ============================================================

if __name__ == "__main__":
    import sys as _sys
    _sys.stdout.reconfigure(encoding='utf-8')
    pytest.main([__file__, "-v", "--tb=short"])
