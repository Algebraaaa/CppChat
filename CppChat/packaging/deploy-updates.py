"""Publish platform-specific update artifacts through the existing Nginx HTTPS site."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import uuid


def atomic_write(path: Path, data: bytes) -> None:
    temporary = path.with_name(path.name + "." + uuid.uuid4().hex + ".tmp")
    try:
        temporary.write_bytes(data)
        temporary.chmod(0o644)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def publish(
    incoming: Path,
    version: str,
    *,
    root: Path = Path("/opt/cppchat/updates"),
    site: Path = Path("/etc/nginx/sites-available/cppchat-https"),
    snippet: Path = Path("/etc/nginx/snippets/cppchat-updates.conf"),
    backups: Path = Path("/opt/cppchat/backups"),
    platform: str = "windows",
) -> None:
    if incoming.parent != root or not incoming.name.startswith(".incoming-"):
        raise ValueError(
            "Incoming artifacts must be inside the managed update directory"
        )
    if not re.fullmatch(
        r"(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})", version
    ):
        raise ValueError("Invalid release version")
    if platform not in ("windows", "android"):
        raise ValueError("Unknown update platform")
    installer_name = (
        f"CppChat-Setup-{version}-x64.exe"
        if platform == "windows"
        else f"CppChat-{version}-android-arm64.apk"
    )
    installer = incoming / installer_name
    manifest_bytes = (incoming / f"CppChat-Update-{version}.json").read_bytes()
    manifest = json.loads(manifest_bytes)
    if (
        manifest["version"] != version
        or manifest["downloadUrl"] != "./" + installer_name
    ):
        raise ValueError("Release manifest does not match installer")
    if (
        installer.stat().st_size != manifest["size"]
        or not 0 < manifest["size"] <= 512 * 1024 * 1024
    ):
        raise ValueError("Installer size does not match manifest")
    with installer.open("rb") as source:
        if hashlib.file_digest(source, "sha256").hexdigest() != manifest["sha256"]:
            raise ValueError("Installer SHA256 does not match manifest")
    destination = root / platform
    destination.mkdir(mode=0o755, parents=True, exist_ok=True)
    latest = destination / "latest.json"
    if latest.exists():
        previous = json.loads(latest.read_bytes())
        if tuple(map(int, previous["version"].split("."))) > tuple(
            map(int, version.split("."))
        ):
            raise ValueError("Refusing to publish an older version as latest")
    final_installer = destination / installer_name
    if final_installer.exists():
        with final_installer.open("rb") as source:
            if hashlib.file_digest(source, "sha256").hexdigest() != manifest["sha256"]:
                raise ValueError(
                    "Version already published with different content; bump the version"
                )

    include = "    include /etc/nginx/snippets/cppchat-updates.conf;\n"
    old_site = site.read_bytes()
    old_snippet = snippet.read_bytes() if snippet.exists() else None
    old_latest = latest.read_bytes() if latest.exists() else None
    backup = backups / ("updates-" + uuid.uuid4().hex)
    backup.mkdir(mode=0o700, parents=True)
    (backup / "cppchat-https.conf").write_bytes(old_site)
    if old_snippet is not None:
        (backup / "cppchat-updates.conf").write_bytes(old_snippet)
    if old_latest is not None:
        (backup / "latest.json").write_bytes(old_latest)
    site_text = old_site.decode("utf-8")
    if include.strip() not in site_text:
        fallback = "    location / { return 404; }"
        if site_text.count(fallback) != 1:
            raise ValueError(
                "Unexpected HTTPS site layout; preserve config and adapt explicitly"
            )
        site_text = site_text.replace(fallback, include + fallback)
    snippet_bytes = b"""# CppChat managed update feeds (HTTPS site only).
location = /updates/windows/latest.json {
    alias /opt/cppchat/updates/windows/latest.json;
    default_type application/json;
    add_header Cache-Control "no-store" always;
    add_header X-Content-Type-Options nosniff always;
    limit_except GET { deny all; }
}
location ~ ^/updates/windows/(CppChat-Setup-[0-9]+\\.[0-9]+\\.[0-9]+-x64\\.exe)$ {
    alias /opt/cppchat/updates/windows/$1;
    default_type application/octet-stream;
    add_header Cache-Control "public, max-age=31536000, immutable";
    add_header X-Content-Type-Options nosniff always;
    limit_except GET { deny all; }
}
location = /updates/android/latest.json {
    alias /opt/cppchat/updates/android/latest.json;
    default_type application/json;
    add_header Cache-Control "no-store" always;
    add_header X-Content-Type-Options nosniff always;
    limit_except GET { deny all; }
}
location ~ ^/updates/android/(CppChat-[0-9]+\\.[0-9]+\\.[0-9]+-android-arm64\\.apk)$ {
    alias /opt/cppchat/updates/android/$1;
    default_type application/vnd.android.package-archive;
    add_header Cache-Control "public, max-age=31536000, immutable";
    add_header X-Content-Type-Options nosniff always;
    limit_except GET { deny all; }
}
"""
    try:
        atomic_write(snippet, snippet_bytes)
        atomic_write(site, site_text.encode("utf-8"))
        subprocess.run(["nginx", "-t"], check=True)
        if not final_installer.exists():
            installer.chmod(0o644)
            os.replace(installer, final_installer)
        # Installer is complete before latest is made visible to clients.
        atomic_write(latest, manifest_bytes)
        subprocess.run(["systemctl", "reload", "nginx"], check=True)
    except Exception:
        atomic_write(site, old_site)
        if old_snippet is None:
            snippet.unlink(missing_ok=True)
        else:
            atomic_write(snippet, old_snippet)
        if old_latest is None:
            latest.unlink(missing_ok=True)
        else:
            atomic_write(latest, old_latest)
        subprocess.run(["nginx", "-t"], check=True)
        subprocess.run(["systemctl", "reload", "nginx"], check=True)
        raise
    print(
        json.dumps(
            {"version": version, "sha256": manifest["sha256"], "backup": str(backup)}
        )
    )
    shutil.rmtree(incoming)  # Validated, exclusively owned staging directory.


if __name__ == "__main__":
    import fcntl  # The deployment entry point runs on the existing Linux server.

    parser = argparse.ArgumentParser()
    parser.add_argument("--incoming", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", choices=("windows", "android"), default="windows")
    arguments = parser.parse_args()
    # Serialize publication so concurrent operators cannot move latest backwards.
    with (arguments.incoming.parent / ".publish.lock").open("a") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        publish(arguments.incoming, arguments.version, platform=arguments.platform)
