"""GitHub draft lookup behavior without network calls or publication."""

import importlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "publish_github", Path(__file__).resolve().parents[2] / "deploy/publish-github.py"
)
publisher = importlib.util.module_from_spec(spec)
with patch.dict(
    sys.modules,
    {
        "linux": importlib.import_module("deploy.linux"),
        "linux.release_bundle": importlib.import_module("deploy.linux.release_bundle"),
    },
):
    spec.loader.exec_module(publisher)


def response(value, code=0):
    return subprocess.CompletedProcess([], code, stdout=json.dumps(value), stderr="")


class ReleaseLookupTests(unittest.TestCase):
    def test_public_release_uses_the_tag_endpoint(self):
        release = {"tag_name": "v1.2.3", "draft": False, "assets": []}
        with patch.object(publisher, "gh", return_value=response(release)) as gh:
            self.assertEqual(publisher.release_api("example/chat", "v1.2.3"), release)
            gh.assert_called_once_with("api", "repos/example/chat/releases/tags/v1.2.3")

    def test_draft_is_found_on_a_later_listing_page(self):
        draft = {"tag_name": "v1.2.3", "draft": True, "assets": []}
        with patch.object(
            publisher,
            "gh",
            side_effect=[response({"status": "404"}, 1), response([[], [draft]])],
        ) as gh:
            self.assertEqual(publisher.release_api("example/chat", "v1.2.3"), draft)
            gh.assert_called_with(
                "api", "repos/example/chat/releases?per_page=100", "--paginate", "--slurp"
            )

    def test_missing_release_returns_none(self):
        with patch.object(
            publisher, "gh", side_effect=[response({"status": "404"}, 1), response([[]])]
        ):
            self.assertIsNone(publisher.release_api("example/chat", "v1.2.3"))

    def test_permission_error_is_not_treated_as_missing(self):
        with patch.object(publisher, "gh", return_value=response({"status": "403"}, 1)) as gh:
            with self.assertRaisesRegex(RuntimeError, "lookup failed"):
                publisher.release_api("example/chat", "v1.2.3")
            self.assertEqual(gh.call_count, 1)
