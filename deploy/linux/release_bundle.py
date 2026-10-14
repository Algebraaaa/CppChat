"""Validate immutable CI artifacts without reading runtime credentials."""

import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import tarfile

BINARIES = ("GateServer", "StatusServer", "ChatServer", "ChatServer2", "ResourceServer")
VERSION = re.compile(r"^(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")


def sha256(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def extract_verified(archive_path, destination, version, commit):
    if not VERSION.fullmatch(version) or not COMMIT.fullmatch(commit):
        raise ValueError("Invalid release identity")
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=False)
    with tarfile.open(archive_path, "r:gz") as archive:
        names = set()
        for member in archive.getmembers():
            path = PurePosixPath(member.name)
            if (
                path.is_absolute()
                or ".." in path.parts
                or "\\" in member.name
                or not (member.isdir() or member.isfile())
                or member.name in names
            ):
                raise ValueError("Unsafe or duplicate archive entry")
            names.add(member.name)
        archive.extractall(destination, filter="data")
    manifest = json.loads((destination / "manifest.json").read_text())
    if (
        manifest.get("version") != version
        or manifest.get("commit") != commit
        or manifest.get("platform") != "ubuntu-24.04-x86_64"
    ):
        raise ValueError("Artifact identity or platform mismatch")
    actual = {
        p.relative_to(destination).as_posix(): sha256(p)
        for p in destination.rglob("*")
        if p.is_file() and p.relative_to(destination).as_posix() != "manifest.json"
    }
    if actual != manifest.get("files"):
        raise ValueError("Artifact contents do not match SHA256 manifest")
    for name in BINARIES:
        header = (destination / "bin" / name).read_bytes()[:20]
        if len(header) < 20 or header[:5] != b"\x7fELF\x02" or header[18:20] != b"\x3e\x00":
            raise ValueError("Expected Linux x86_64 ELF executable")
    if "source.tar.gz" not in actual or "VerifyServer/package-lock.json" not in actual:
        raise ValueError("Source archive or locked Node dependencies missing")
    return manifest


def verify_update(directory, version):
    directory = Path(directory)
    installer = directory / f"CppChat-Setup-{version}-x64.exe"
    manifest = json.loads((directory / f"CppChat-Update-{version}.json").read_text())
    if (
        manifest.get("version") != version
        or manifest.get("downloadUrl") != "./" + installer.name
        or manifest.get("size") != installer.stat().st_size
        or manifest.get("sha256") != sha256(installer)
    ):
        raise ValueError("Windows update artifact mismatch")
    checksum = (directory / (installer.name + ".sha256")).read_text().split()
    if checksum != [manifest["sha256"], installer.name]:
        raise ValueError("Windows checksum file mismatch")
    return manifest
