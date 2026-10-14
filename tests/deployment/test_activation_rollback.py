"""Inject a mid-rollout failure and verify restoration without real services."""

import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import uuid

import test_release_bundle as fixtures
from deploy.linux import release_bundle


@unittest.skipIf(os.name == "nt", "Linux deployment transaction; exercised on the Ubuntu CI runner")
class ActivationRollbackTests(unittest.TestCase):
    def test_mid_rollout_failure_restores_programs_modules_source_and_feed(self):
        fixture = fixtures.ReleaseBundleTests("test_valid_linux_manifest")
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        root = fixture.root / "production"
        incoming = fixture.root / "incoming/job"
        incoming.mkdir(parents=True)
        (root / "backups").mkdir(parents=True)
        (root / "repository").mkdir()
        (root / "build/bin").mkdir(parents=True)
        (root / "source").mkdir()
        (root / "source/content.txt").write_text("previous source")
        (root / "source-version.json").write_text(
            json.dumps({"release_version": "1.2.2", "runtime_commit": "0" * 40})
        )
        for name in (*release_bundle.BINARIES, "VerifyServer", "GatewayGuard"):
            folder = root / "runtime" / name
            folder.mkdir(parents=True)
            (folder / "config.json").write_text("{}")
        (root / "runtime/GateServer/config.ini").write_text("[GateServer]\nPort=18080\n")
        for name in release_bundle.BINARIES:
            (root / "build/bin" / name).write_bytes(b"previous-" + name.encode())
        verifier = root / "runtime/VerifyServer"
        (verifier / "email.js").write_text("private SMTP override")
        (verifier / "server.js").write_text("previous verifier")
        (verifier / "node_modules").mkdir()
        (verifier / "node_modules/old.js").write_text("previous dependency")
        (root / "runtime/GatewayGuard/server.js").write_text("previous guard")
        latest = root / "updates/windows/latest.json"
        latest.parent.mkdir(parents=True)
        latest.write_text('{"version":"1.2.2"}')
        prior_feed = latest.read_bytes()
        source = fixture.root / "new-source"
        source.mkdir()
        (source / "content.txt").write_text("new source")

        def prepare_payload(payload):
            with tarfile.open(payload / "source.tar.gz", "w:gz") as archive:
                archive.add(source / "content.txt", arcname="content.txt")
            for name in (
                "server.js",
                "config.js",
                "redis.js",
                "const.js",
                "proto.js",
                "message.proto",
                "package.json",
            ):
                (payload / "VerifyServer" / name).write_text("{}")
            (payload / "GatewayGuard").mkdir()
            (payload / "GatewayGuard/server.js").write_text("{}")
            manifest_path = payload / "manifest.json"
            manifest = json.loads(manifest_path.read_text())
            manifest["files"] = {
                p.relative_to(payload).as_posix(): release_bundle.sha256(p)
                for p in payload.rglob("*")
                if p.is_file() and p != manifest_path
            }
            manifest_path.write_text(json.dumps(manifest))

        artifact = fixture.bundle(modify=prepare_payload)
        server_name = f"CppChat-Server-{fixture.version}-linux-x64.tar.gz"
        shutil.copy2(artifact, incoming / server_name)
        (incoming / (server_name + ".sha256")).write_text(
            release_bundle.sha256(artifact) + "  " + server_name
        )
        installer = incoming / f"CppChat-Setup-{fixture.version}-x64.exe"
        installer.write_bytes(b"fixture installer")
        digest = release_bundle.sha256(installer)
        (incoming / (installer.name + ".sha256")).write_text(digest + "  " + installer.name)
        (incoming / f"CppChat-Update-{fixture.version}.json").write_text(
            json.dumps(
                {
                    "version": fixture.version,
                    "downloadUrl": "./" + installer.name,
                    "size": installer.stat().st_size,
                    "sha256": digest,
                }
            )
        )
        spec = importlib.util.spec_from_file_location(
            "activation_under_test", fixtures.ROOT / "deploy/linux/activate_release.py"
        )
        controller = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"release_bundle": release_bundle}):
            spec.loader.exec_module(controller)

        def run(arguments, **kwargs):
            if arguments[0] == "git" and "archive" in arguments:
                shutil.copy2(
                    fixture.root / "payload/source.tar.gz", arguments[arguments.index("-o") + 1]
                )
            elif arguments[0] == "npm":
                modules = Path(arguments[arguments.index("--prefix") + 1]) / "node_modules"
                modules.mkdir()
                (modules / "new.js").write_text("new dependency")
            elif arguments[0] == "rsync":
                source_dir, target_dir = Path(arguments[-2]), Path(arguments[-1])
                if target_dir.exists():
                    target_dir.rename(fixture.root / ("replaced-" + uuid.uuid4().hex))
                shutil.copytree(source_dir, target_dir)
            elif arguments == ["systemctl", "start", "cppchat-ChatServer2"]:
                raise subprocess.CalledProcessError(1, arguments)
            return SimpleNamespace(stdout="all libraries present")

        with (
            patch.object(controller, "ROOT", root),
            patch.object(controller, "INCOMING", incoming.parent),
            patch.object(controller, "run", side_effect=run),
            patch.object(controller.os, "chown"),
            patch.object(
                controller.pwd,
                "getpwnam",
                return_value=SimpleNamespace(pw_uid=os.getuid(), pw_gid=os.getgid()),
            ),
            patch.object(controller.subprocess, "check_output", return_value=b""),
            patch.object(controller.time, "sleep"),
        ):
            with self.assertRaises(subprocess.CalledProcessError):
                controller.activate(incoming, fixture.version, fixture.commit)
        for name in release_bundle.BINARIES:
            self.assertEqual((root / "build/bin" / name).read_bytes(), b"previous-" + name.encode())
        self.assertFalse((verifier / "node_modules").is_symlink())
        self.assertEqual((verifier / "node_modules/old.js").read_text(), "previous dependency")
        self.assertEqual((verifier / "server.js").read_text(), "previous verifier")
        self.assertEqual((verifier / "email.js").read_text(), "private SMTP override")
        self.assertEqual((root / "source/content.txt").read_text(), "previous source")
        self.assertEqual(latest.read_bytes(), prior_feed)
        self.assertEqual(
            json.loads((root / "source-version.json").read_text())["release_version"], "1.2.2"
        )
