"""Isolated release transaction tests; never connect to SSH or reload real Nginx."""

import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

REPOSITORY = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "deploy_updates", REPOSITORY / "CppChat/packaging/deploy-updates.py"
)
deploy = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(deploy)


class PublishTests(unittest.TestCase):
    platform = "windows"

    def setUp(self):
        work = REPOSITORY / "build/packaging/publish-tests"
        work.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.root = self.directory / "updates"
        self.incoming = self.root / ".incoming-test"
        self.incoming.mkdir(parents=True)
        self.destination = self.root / self.platform
        self.destination.mkdir()
        self.site = self.directory / "site.conf"
        self.original_site = b"server {\n    location / { return 404; }\n}\n"
        self.site.write_bytes(self.original_site)
        self.snippet = self.directory / "snippet.conf"
        self.snippet.write_bytes(b"# previous snippet\n")
        self.latest = self.destination / "latest.json"
        self.latest.write_text(json.dumps({"version": "0.1.0"}), encoding="utf-8")
        self.original_latest = self.latest.read_bytes()
        self.installer_name = (
            "CppChat-Setup-0.1.1-x64.exe"
            if self.platform == "windows"
            else "CppChat-0.1.1-android-arm64.apk"
        )
        self.installer = b"owned installer bytes"
        (self.incoming / self.installer_name).write_bytes(self.installer)
        self.manifest = {
            "version": "0.1.1",
            "notes": "测试发布",
            "downloadUrl": "./" + self.installer_name,
            "size": len(self.installer),
            "sha256": hashlib.sha256(self.installer).hexdigest(),
        }
        (self.incoming / "CppChat-Update-0.1.1.json").write_text(
            json.dumps(self.manifest), encoding="utf-8"
        )

    def publish(self):
        deploy.publish(
            self.incoming,
            "0.1.1",
            root=self.root,
            site=self.site,
            snippet=self.snippet,
            backups=self.directory / "backups",
            platform=self.platform,
        )

    def test_installer_precedes_latest_and_staging_is_cleaned(self):
        def run(command, **_):
            if command == ["nginx", "-t"]:
                self.assertEqual(self.latest.read_bytes(), self.original_latest)
            if command == ["systemctl", "reload", "nginx"]:
                self.assertEqual(
                    (self.destination / self.installer_name).read_bytes(), self.installer
                )
                self.assertEqual(json.loads(self.latest.read_bytes()), self.manifest)

        with patch.object(deploy.subprocess, "run", side_effect=run) as process:
            self.publish()
        self.assertEqual(process.call_count, 2)
        self.assertFalse(self.incoming.exists())
        self.assertIn(b"include /etc/nginx/snippets/cppchat-updates.conf", self.site.read_bytes())
        backup = next((self.directory / "backups").iterdir())
        self.assertEqual((backup / "latest.json").read_bytes(), self.original_latest)

    def test_tampering_is_rejected_before_configuration_changes(self):
        (self.incoming / self.installer_name).write_bytes(b"changed installer!!!")
        with patch.object(deploy.subprocess, "run") as process:
            with self.assertRaises(ValueError):
                self.publish()
        process.assert_not_called()
        self.assertEqual(self.site.read_bytes(), self.original_site)
        self.assertEqual(self.latest.read_bytes(), self.original_latest)

    def test_published_version_is_immutable(self):
        (self.destination / self.installer_name).write_bytes(b"different release")
        with self.assertRaisesRegex(ValueError, "Version already published"):
            self.publish()

    def test_no_accidental_downgrade(self):
        self.latest.write_text('{"version":"0.2.0"}', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "older version"):
            self.publish()

    def test_config_validation_failure_restores_state(self):
        calls = 0

        def run(command, **_):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise subprocess.CalledProcessError(1, command)

        with patch.object(deploy.subprocess, "run", side_effect=run):
            with self.assertRaises(subprocess.CalledProcessError):
                self.publish()
        self.assertEqual(self.site.read_bytes(), self.original_site)
        self.assertEqual(self.snippet.read_bytes(), b"# previous snippet\n")
        self.assertEqual(self.latest.read_bytes(), self.original_latest)
        self.assertTrue(self.incoming.exists())

    def test_reload_failure_rolls_back_latest(self):
        calls = 0

        def run(command, **_):
            nonlocal calls
            calls += 1
            if calls == 2:
                raise subprocess.CalledProcessError(1, command)

        with patch.object(deploy.subprocess, "run", side_effect=run):
            with self.assertRaises(subprocess.CalledProcessError):
                self.publish()
        self.assertEqual(self.site.read_bytes(), self.original_site)
        self.assertEqual(self.latest.read_bytes(), self.original_latest)
        self.assertTrue(self.incoming.exists())


class AndroidPublishTests(PublishTests):
    platform = "android"

    def test_android_publication_preserves_windows_feed(self):
        windows = self.root / "windows"
        windows.mkdir()
        current = b'{"version":"0.1.18"}'
        (windows / "latest.json").write_bytes(current)
        with patch.object(deploy.subprocess, "run"):
            self.publish()
        self.assertEqual((windows / "latest.json").read_bytes(), current)
        self.assertIn(b"/updates/android/latest.json", self.snippet.read_bytes())


if __name__ == "__main__":
    unittest.main()
