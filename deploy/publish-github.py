"""Use GitHub CLI's release API; never overwrite a published asset."""

import argparse
import json
from pathlib import Path
import re
import subprocess

from linux.release_bundle import VERSION, sha256, verify_update


def gh(*args):
    return subprocess.run(["gh", *args], text=True, capture_output=True)


def release_api(repo, tag):
    result = gh("api", f"repos/{repo}/releases/tags/{tag}")
    value = json.loads(result.stdout)
    if result.returncode:
        if str(value.get("status")) == "404":
            return None
        raise RuntimeError("GitHub release lookup failed")
    return value


def publish(repo, tag, version, assets):
    if (
        not VERSION.fullmatch(version)
        or tag != "v" + version
        or not re.fullmatch(r"[\w.-]+/[\w.-]+", repo)
    ):
        raise ValueError("Invalid repository or release version")
    verify_update(assets, version)
    names = [
        f"CppChat-Setup-{version}-x64.exe",
        f"CppChat-Setup-{version}-x64.exe.sha256",
        f"CppChat-Update-{version}.json",
        f"CppChat-Server-{version}-linux-x64.tar.gz",
        f"CppChat-Server-{version}-linux-x64.tar.gz.sha256",
    ]
    if sorted(p.name for p in assets.iterdir() if p.is_file()) != sorted(names):
        raise ValueError("Unexpected or missing release asset")
    server = assets / names[3]
    if (assets / names[4]).read_text().split() != [sha256(server), server.name]:
        raise ValueError("Server artifact checksum mismatch")
    release = release_api(repo, tag)
    if release is None:
        notes = assets.parent / "release-notes.md"
        notes.write_text(
            f"CppChat {version}\n\nWindows x64 安装包，包含聊天、Windows 系统配色适配、HTTPS/TLS、本地数据加密和备份恢复。\n\n"
            "安装包当前未进行代码签名。换电脑或重置 Windows 前，请先导出恢复密钥并保存恢复密码。\n\n"
            "EXE 为用户安装包；Linux tar.gz 为服务端发布产物。SHA256 文件用于核对下载完整性。\n",
            encoding="utf-8",
        )
        subprocess.run(
            [
                "gh",
                "release",
                "create",
                tag,
                "--repo",
                repo,
                "--verify-tag",
                "--draft",
                "--title",
                f"CppChat {version}",
                "--notes-file",
                str(notes),
            ],
            check=True,
        )
        release = release_api(repo, tag)
    existing = {asset["name"]: asset for asset in release["assets"]}
    for name in names:
        local = assets / name
        if name in existing:
            digest = existing[name].get("digest")
            if digest is None:
                download = assets.parent / "existing-assets"
                download.mkdir(exist_ok=True)
                subprocess.run(
                    [
                        "gh",
                        "release",
                        "download",
                        tag,
                        "--repo",
                        repo,
                        "--pattern",
                        name,
                        "--dir",
                        str(download),
                    ],
                    check=True,
                )
                digest = "sha256:" + sha256(download / name)
            if digest != "sha256:" + sha256(local):
                raise ValueError("Published asset differs; bump the version instead of overwriting")
        else:
            if not release["draft"]:
                raise ValueError("Published release is incomplete; a new version is required")
            subprocess.run(["gh", "release", "upload", tag, str(local), "--repo", repo], check=True)
    if release["draft"]:
        subprocess.run(
            ["gh", "release", "edit", tag, "--repo", repo, "--draft=false", "--latest"], check=True
        )
    final = release_api(repo, tag)
    if final["draft"] or sorted(a["name"] for a in final["assets"]) != sorted(names):
        raise RuntimeError("GitHub release verification failed")
    print(json.dumps({"tag": tag, "url": final["html_url"], "assets": names}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--assets", type=Path, required=True)
    args = parser.parse_args()
    publish(args.repo, args.tag, args.version, args.assets)
