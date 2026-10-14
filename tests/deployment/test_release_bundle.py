"""Artifact boundaries; no SSH, runtime configuration or production services."""

import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest

from deploy.linux.release_bundle import BINARIES, extract_verified, sha256, verify_update

ROOT = Path(__file__).resolve().parents[2]


class ReleaseBundleTests(unittest.TestCase):
    def setUp(self):
        work = ROOT / "build/regression/diagnostics/cd-validator"
        work.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=work)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.version, self.commit = "1.2.3", "a" * 40

    def bundle(self, modify=None, extra=None):
        payload = self.root / "payload"
        (payload / "bin").mkdir(parents=True)
        (payload / "VerifyServer").mkdir()
        elf = bytearray(64)
        elf[:5], elf[18:20] = b"\x7fELF\x02", b"\x3e\x00"
        for name in BINARIES:
            (payload / "bin" / name).write_bytes(elf)
        (payload / "source.tar.gz").write_bytes(b"source-fixture")
        (payload / "VerifyServer/package-lock.json").write_text("{}")
        manifest = {
            "version": self.version,
            "commit": self.commit,
            "platform": "ubuntu-24.04-x86_64",
            "files": {
                p.relative_to(payload).as_posix(): sha256(p)
                for p in payload.rglob("*")
                if p.is_file()
            },
        }
        (payload / "manifest.json").write_text(json.dumps(manifest))
        if modify:
            modify(payload)
        target = self.root / "artifact.tar.gz"
        with tarfile.open(target, "w:gz") as archive:
            for path in payload.rglob("*"):
                if path.is_file():
                    archive.add(path, arcname=path.relative_to(payload).as_posix())
            if extra:
                archive.addfile(extra, io.BytesIO(b"x") if extra.isfile() else None)
        return target

    def test_valid_linux_manifest(self):
        result = extract_verified(self.bundle(), self.root / "output", self.version, self.commit)
        self.assertEqual(result["commit"], self.commit)

    def test_reject_tampered_binary(self):
        artifact = self.bundle(lambda p: (p / "bin/ChatServer").write_bytes(b"changed"))
        with self.assertRaisesRegex(ValueError, "SHA256"):
            extract_verified(artifact, self.root / "output", self.version, self.commit)

    def test_reject_wrong_commit(self):
        with self.assertRaisesRegex(ValueError, "identity"):
            extract_verified(self.bundle(), self.root / "output", self.version, "b" * 40)

    def test_reject_archive_path_escape(self):
        extra = tarfile.TarInfo("../outside")
        extra.size = 1
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            extract_verified(
                self.bundle(extra=extra), self.root / "output", self.version, self.commit
            )
        self.assertFalse((self.root / "outside").exists())

    def test_reject_symlink(self):
        extra = tarfile.TarInfo("link")
        extra.type, extra.linkname = tarfile.SYMTYPE, "bin/GateServer"
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            extract_verified(
                self.bundle(extra=extra), self.root / "output", self.version, self.commit
            )

    def test_reject_unlisted_nested_manifest(self):
        artifact = self.bundle(lambda p: (p / "bin/manifest.json").write_text("{}"))
        with self.assertRaisesRegex(ValueError, "SHA256"):
            extract_verified(artifact, self.root / "output", self.version, self.commit)

    def test_reject_windows_hash_mismatch(self):
        installer = self.root / f"CppChat-Setup-{self.version}-x64.exe"
        installer.write_bytes(b"installer-fixture")
        (self.root / f"CppChat-Update-{self.version}.json").write_text(
            json.dumps(
                {
                    "version": self.version,
                    "downloadUrl": "./" + installer.name,
                    "size": installer.stat().st_size,
                    "sha256": "0" * 64,
                }
            )
        )
        with self.assertRaisesRegex(ValueError, "mismatch"):
            verify_update(self.root, self.version)


if __name__ == "__main__":
    unittest.main()
