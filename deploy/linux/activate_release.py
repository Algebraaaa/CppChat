"""Root-owned CD entry point; sudo permits this command, not a general root shell."""

import argparse
import configparser
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import pwd
import shutil
import subprocess
import tarfile
import time
import urllib.request
import uuid

from release_bundle import BINARIES, COMMIT, VERSION, extract_verified, sha256, verify_update

ROOT = Path("/opt/cppchat")
INCOMING = Path("/var/lib/cppchat-deploy/incoming")
SERVICES = (*BINARIES, "VerifyServer", "GatewayGuard")


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, **kwargs)


def extract_source(archive_path, destination):
    destination.mkdir()
    with tarfile.open(archive_path) as archive:
        for member in archive.getmembers():
            name = Path(member.name)
            if name.is_absolute() or ".." in name.parts or not (member.isfile() or member.isdir()):
                raise ValueError("Unsafe source archive")
        archive.extractall(destination, filter="data")


def restore_tar(archive_path, destination):
    with tarfile.open(archive_path) as archive:
        archive.extractall(destination, filter="data")
        for member in archive.getmembers():
            target = destination / member.name
            if target.exists() and not target.is_symlink():
                os.chown(target, member.uid, member.gid)
                os.chmod(target, member.mode)


def health():
    run(
        [
            "systemctl",
            "is-active",
            "--quiet",
            *(f"cppchat-{name}" for name in SERVICES),
            "cppchat-redis",
            "mysql",
        ]
    )
    run(["nginx", "-t"])
    config = configparser.ConfigParser()
    config.read(ROOT / "runtime/GateServer/config.ini")
    port = config["GateServer"].getint("Port")
    request = urllib.request.Request(
        f"http://127.0.0.1:{port}/user_login",
        data=json.dumps(
            {"email": uuid.uuid4().hex + "@tests.invalid", "passwd": uuid.uuid4().hex}
        ).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=20) as response:
        if json.load(response).get("error") != 1009:
            raise RuntimeError("Gateway database readiness failed")


def activate(incoming, version, commit):
    if incoming.is_symlink():
        raise ValueError("Incoming symlinks are forbidden")
    incoming = incoming.resolve()
    if incoming.parent != INCOMING.resolve() or not incoming.is_dir():
        raise ValueError("Incoming must be one owned deployment directory")
    if not VERSION.fullmatch(version) or not COMMIT.fullmatch(commit):
        raise ValueError("Invalid release identity")
    current_path = ROOT / "source-version.json"
    current = json.loads(current_path.read_text()) if current_path.exists() else {}
    if current.get("release_version") == version:
        if current.get("runtime_commit") != commit:
            raise ValueError("Version already deployed from a different commit")
        health()
        print(json.dumps({"version": version, "commit": commit, "status": "already-deployed"}))
        return
    latest = ROOT / "updates/windows/latest.json"
    if latest.exists():
        old_version = json.loads(latest.read_text())["version"]
        if tuple(map(int, version.split("."))) <= tuple(map(int, old_version.split("."))):
            raise ValueError("Release must advance the existing update version")
    identifier = version + "-" + commit[:12] + "-" + uuid.uuid4().hex[:8]
    work = ROOT / "releases" / identifier
    work.mkdir(mode=0o750, parents=True)
    service_owner = pwd.getpwnam("cppchat")
    os.chown(work, 0, service_owner.pw_gid)
    # Copy to a root-owned directory before validation: uploads cannot change checked bytes.
    copied = work / "incoming"
    shutil.copytree(incoming, copied, symlinks=True)
    if any(p.is_symlink() for p in copied.rglob("*")):
        raise ValueError("Incoming symlinks are forbidden")
    update = verify_update(copied, version)
    server_name = f"CppChat-Server-{version}-linux-x64.tar.gz"
    server_archive = copied / server_name
    if (copied / (server_name + ".sha256")).read_text().split() != [
        sha256(server_archive),
        server_name,
    ]:
        raise ValueError("Server archive checksum mismatch")
    payload = work / "payload"
    manifest = extract_verified(server_archive, payload, version, commit)
    for name in BINARIES:
        result = run(["ldd", str(payload / "bin" / name)], capture_output=True, text=True)
        if "not found" in result.stdout:
            raise RuntimeError("Release requires unavailable system libraries")
    source = work / "source"
    extract_source(payload / "source.tar.gz", source)
    # The source archive must be the declared Git commit, not an unrelated upload.
    checkout = ROOT / "repository"
    if subprocess.check_output(["git", "-C", str(checkout), "status", "--porcelain"]).strip():
        raise RuntimeError("Cloud checkout has local edits")
    run(["git", "-C", str(checkout), "fetch", "origin"])
    run(["git", "-C", str(checkout), "merge-base", "--is-ancestor", commit, "origin/main"])
    git_archive = work / "git-source.tar.gz"
    run(
        [
            "git",
            "-C",
            str(checkout),
            "-c",
            "core.autocrlf=false",
            "archive",
            "--format=tar.gz",
            "-o",
            str(git_archive),
            commit,
        ]
    )
    git_source = work / "git-source"
    extract_source(git_archive, git_source)
    source_hashes = {
        p.relative_to(source).as_posix(): sha256(p) for p in source.rglob("*") if p.is_file()
    }
    git_hashes = {
        p.relative_to(git_source).as_posix(): sha256(p)
        for p in git_source.rglob("*")
        if p.is_file()
    }
    if source_hashes != git_hashes:
        raise ValueError("Source archive does not match the Git commit")
    verifier = payload / "VerifyServer"
    run(["npm", "ci", "--prefix", str(verifier), "--omit=dev", "--ignore-scripts"])
    for path in verifier.glob("*.js"):
        run(["node", "--check", str(path)])
    for folder in (payload, verifier):
        os.chown(folder, 0, service_owner.pw_gid)
        os.chmod(folder, 0o750)
    for path in (verifier / "node_modules").rglob("*"):
        if path.is_dir():
            os.chmod(path, 0o755)
    backup = ROOT / "backups" / ("cd-" + identifier)
    backup.mkdir(mode=0o700)
    with tarfile.open(backup / "source.tar.gz", "w:gz") as archive:
        archive.add(ROOT / "source", arcname="source")
    with tarfile.open(backup / "runtime.tar.gz", "w:gz") as archive:
        for name in SERVICES:
            for path in (ROOT / "runtime" / name).glob("*"):
                if path.is_file() and path.suffix in (".js", ".json", ".ini", ".proto"):
                    archive.add(path, arcname=name + "/" + path.name)
    (backup / "bin").mkdir()
    for name in BINARIES:
        shutil.copy2(ROOT / "build/bin" / name, backup / "bin" / name)
    for path in backup.glob("*.gz"):
        os.chmod(path, 0o600)
    if current_path.exists():
        shutil.copy2(current_path, backup / "source-version.json")
    mail_hash = sha256(ROOT / "runtime/VerifyServer/email.js")
    config_hashes = {
        str(p): sha256(p)
        for p in (ROOT / "runtime").glob("*/config.*")
        if p.is_file() and p.suffix in (".ini", ".json")
    }
    modules = ROOT / "runtime/VerifyServer/node_modules"
    previous_modules = backup / "node_modules"
    module_swapped = False
    previous_feed = latest.read_bytes() if latest.exists() else None
    try:
        run(["git", "-C", str(checkout), "merge", "--ff-only", commit])
        run(["rsync", "-a", "--delete", str(source) + "/", str(ROOT / "source") + "/"])
        run(["systemctl", "stop", "cppchat-VerifyServer", "cppchat-GatewayGuard"])
        if modules.exists() or modules.is_symlink():
            modules.rename(previous_modules)
        modules.symlink_to(verifier / "node_modules", target_is_directory=True)
        module_swapped = True
        for folder, names in [
            (
                "VerifyServer",
                (
                    "server.js",
                    "config.js",
                    "redis.js",
                    "const.js",
                    "proto.js",
                    "message.proto",
                    "package.json",
                    "package-lock.json",
                ),
            ),
            ("GatewayGuard", ("server.js",)),
        ]:
            for name in names:
                target = ROOT / "runtime" / folder / name
                shutil.copyfile(payload / folder / name, target)
                os.chown(target, service_owner.pw_uid, service_owner.pw_gid)
                os.chmod(target, 0o640)
        run(["systemctl", "start", "cppchat-VerifyServer", "cppchat-GatewayGuard"])
        for name in ("StatusServer", "ChatServer", "ChatServer2", "ResourceServer", "GateServer"):
            target = ROOT / "build/bin" / name
            candidate = target.with_name(name + ".cd-new")
            shutil.copy2(payload / "bin" / name, candidate)
            os.chmod(candidate, 0o755)
            run(["systemctl", "stop", "cppchat-" + name])
            os.replace(candidate, target)
            run(["systemctl", "start", "cppchat-" + name])
            time.sleep(1)
            run(["systemctl", "is-active", "--quiet", "cppchat-" + name])
        time.sleep(2)
        health()
        if sha256(ROOT / "runtime/VerifyServer/email.js") != mail_hash or any(
            sha256(Path(p)) != expected for p, expected in config_hashes.items()
        ):
            raise RuntimeError("Private runtime configuration changed")
        feed = ROOT / "updates" / (".incoming-cd-" + uuid.uuid4().hex)
        feed.mkdir(mode=0o755)
        for name in (f"CppChat-Setup-{version}-x64.exe", f"CppChat-Update-{version}.json"):
            shutil.copy2(copied / name, feed / name)
        spec = importlib.util.spec_from_file_location(
            "cppchat_update_publish", ROOT / "source/CppChat/packaging/deploy-updates.py"
        )
        publisher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(publisher)
        publisher.publish(feed, version)
        record = {
            "release_version": version,
            "source_commit": commit,
            "runtime_commit": commit,
            "runtime_rebuilt": True,
            "runtime_restarted": True,
            "backup": str(backup),
            "binary_hashes": {name: manifest["files"]["bin/" + name] for name in BINARIES},
            "windows_sha256": update["sha256"],
            "deployment_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "environment_overrides": [
                "runtime/VerifyServer/email.js",
                "private runtime config files",
            ],
        }
        current_path.write_text(json.dumps(record, indent=2))
        (backup / "deployment.json").write_text(json.dumps(record, indent=2))
        incoming.rename(backup / "uploaded-artifacts")
        print(
            json.dumps(
                {"version": version, "commit": commit, "status": "deployed", "backup": str(backup)}
            )
        )
    except Exception:
        run(["systemctl", "stop", *(f"cppchat-{name}" for name in SERVICES)])
        for name in BINARIES:
            shutil.copy2(backup / "bin" / name, ROOT / "build/bin" / name)
        restore_tar(backup / "runtime.tar.gz", ROOT / "runtime")
        if module_swapped and modules.is_symlink():
            modules.unlink()
        if previous_modules.exists() or previous_modules.is_symlink():
            previous_modules.rename(modules)
        restore_tar(backup / "source.tar.gz", backup / "recovered")
        run(
            [
                "rsync",
                "-a",
                "--delete",
                str(backup / "recovered/source") + "/",
                str(ROOT / "source") + "/",
            ]
        )
        if (backup / "source-version.json").exists():
            shutil.copy2(backup / "source-version.json", current_path)
        run(["systemctl", "start", *(f"cppchat-{name}" for name in SERVICES)])
        if previous_feed is not None:
            restored_feed = latest.with_name(".latest.rollback-" + uuid.uuid4().hex)
            restored_feed.write_bytes(previous_feed)
            os.chmod(restored_feed, 0o644)
            os.replace(restored_feed, latest)
        (backup / "FAILED").write_text(
            "Deployment failed; runtime and source export rolled back. See CI job logs.\n"
        )
        raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--incoming", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    if os.geteuid() != 0:
        raise PermissionError("Use the dedicated sudo deployment entry point")
    with (
        (ROOT / ".cd.lock").open("a") as lock,
        (ROOT / "updates/.publish.lock").open("a") as feed_lock,
    ):
        fcntl.flock(lock, fcntl.LOCK_EX)
        fcntl.flock(feed_lock, fcntl.LOCK_EX)
        activate(args.incoming, args.version, args.commit)


if __name__ == "__main__":
    main()
