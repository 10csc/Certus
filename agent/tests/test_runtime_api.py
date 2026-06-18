# -*- coding: utf-8 -*-
"""Runtime API tests."""

import os
import sys

SCRIPT_DIR = os.path.join(os.path.dirname(__file__), "..", "scripts")
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)


def test_status_response_has_runtime_paths():
    from runtime_api import handle_request

    response = handle_request({"action": "status"})

    assert response["ok"] is True
    assert response["runtime_home"]
    assert response["data_dir"]
    assert response["config_path"]


def test_unknown_action_is_structured_error():
    from runtime_api import handle_request

    response = handle_request({"action": "nope"})

    assert response["ok"] is False
    assert "unknown action" in response["error"]


def test_search_requires_query():
    from runtime_api import handle_request

    response = handle_request({"action": "search"})

    assert response["ok"] is False
    assert response["error"] == "query is required"
