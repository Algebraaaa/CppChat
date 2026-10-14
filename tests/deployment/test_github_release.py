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
deployment_spec = importlib.util.spec_from_file_location(
    "run_deployment", Path(__file__).resolve().parents[2] / "deploy/run-deployment.py"
)
deployment = importlib.util.module_from_spec(deployment_spec)
with patch.dict(
    sys.modules,
    {
        "linux": importlib.import_module("deploy.linux"),
        "linux.release_bundle": importlib.import_module("deploy.linux.release_bundle"),
    },
):
    spec.loader.exec_module(publisher)
    deployment_spec.loader.exec_module(deployment)


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

    def test_creation_visibility_delay_is_retried_without_recreating(self):
        draft = {"draft": True}
        with (
            patch.object(publisher, "release_api", side_effect=[None, None, draft]) as read,
            patch.object(publisher.time, "sleep") as sleep,
        ):
            self.assertEqual(publisher.wait_for_release("example/chat", "v1.2.3"), draft)
            self.assertEqual(read.call_count, 3)
            self.assertEqual([call.args[0] for call in sleep.call_args_list], [2, 4])

    def test_publication_waits_until_the_draft_becomes_public(self):
        public = {"draft": False}
        with (
            patch.object(publisher, "release_api", side_effect=[{"draft": True}, public]),
            patch.object(publisher.time, "sleep"),
        ):
            self.assertEqual(
                publisher.wait_for_release("example/chat", "v1.2.3", published=True), public
            )

    def test_visibility_retry_is_bounded(self):
        with (
            patch.object(publisher, "release_api", return_value=None) as read,
            patch.object(publisher.time, "sleep"),
        ):
            with self.assertRaisesRegex(RuntimeError, "bounded retry window"):
                publisher.wait_for_release("example/chat", "v1.2.3")
            self.assertEqual(read.call_count, 7)


class SshMaterialTests(unittest.TestCase):
    def test_windows_newlines_are_normalized_for_openssh(self):
        value = "-----BEGIN OPENSSH PRIVATE KEY-----\r\nexample\r\n-----END OPENSSH PRIVATE KEY-----\r\n"
        expected = value.replace("\r\n", "\n")
        self.assertEqual(deployment.ssh_text(value), expected)
        self.assertEqual(deployment.ssh_text(expected), expected)
