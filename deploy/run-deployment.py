"""Transport verified assets using the dedicated CI SSH identity."""

import argparse
import configparser
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import urllib.request
import uuid

from linux.release_bundle import COMMIT, VERSION, verify_update


def ssh_text(value):
    # OpenSSH's private key parser requires LF even when Secrets were set on Windows.
    return value.replace("\r\n", "\n").replace("\r", "\n").strip() + "\n"


def deploy(assets, version, commit):
    if not VERSION.fullmatch(version) or not COMMIT.fullmatch(commit):
        raise ValueError("Invalid release identity")
    update = verify_update(assets, version)
    host, user, port = [os.environ[name] for name in ("DEPLOY_HOST", "DEPLOY_USER", "DEPLOY_PORT")]
    if (
        not re.fullmatch(r"[A-Za-z0-9.-]+", host)
        or not re.fullmatch(r"[a-z_][a-z0-9_-]*", user)
        or not port.isdigit()
        or not 1 <= int(port) <= 65535
    ):
        raise ValueError("Invalid SSH destination")
    config = configparser.ConfigParser()
    config.read_string(os.environ["CLIENT_CONFIG"])
    if config["GateServer"].get("scheme") != "https":
        raise ValueError("Public readiness check requires HTTPS")
    identifier = version + "-" + uuid.uuid4().hex
    incoming = "/var/lib/cppchat-deploy/incoming/" + identifier
    with tempfile.TemporaryDirectory(
        prefix="cppchat-cd-", dir=os.environ.get("RUNNER_TEMP")
    ) as temporary:
        private = Path(temporary) / "identity"
        known = Path(temporary) / "known_hosts"
        private.write_text(ssh_text(os.environ["DEPLOY_SSH_KEY"]), encoding="utf-8", newline="\n")
        known.write_text(ssh_text(os.environ["DEPLOY_KNOWN_HOSTS"]), encoding="utf-8", newline="\n")
        os.chmod(private, 0o600)
        os.chmod(known, 0o600)
        options = [
            "-i",
            str(private),
            "-o",
            "IdentitiesOnly=yes",
            "-o",
            "BatchMode=yes",
            "-o",
            "StrictHostKeyChecking=yes",
            "-o",
            "UserKnownHostsFile=" + str(known),
            "-o",
            "ConnectTimeout=20",
        ]
        remote = user + "@" + host
        subprocess.run(
            ["ssh", *options, "-p", port, remote, "install -d -m 700 " + incoming], check=True
        )
        files = sorted(p for p in assets.iterdir() if p.is_file())
        subprocess.run(
            ["scp", *options, "-P", port, *map(str, files), remote + ":" + incoming + "/"],
            check=True,
        )
        command = (
            "sudo -n /usr/bin/python3 /opt/cppchat/deploy/cd/activate_release.py --incoming "
            + incoming
            + " --version "
            + version
            + " --commit "
            + commit
        )
        subprocess.run(["ssh", *options, "-p", port, remote, command], check=True)
    gateway = config["GateServer"]
    url = "https://" + gateway["host"] + ":" + gateway["port"] + "/updates/windows/latest.json"
    with urllib.request.urlopen(url, timeout=30) as response:
        online = json.load(response)
    if online["version"] != version or online["sha256"] != update["sha256"]:
        raise RuntimeError("Online update feed does not match the release")
    download = url.rsplit("/", 1)[0] + "/CppChat-Setup-" + version + "-x64.exe"
    digest, size = hashlib.sha256(), 0
    with urllib.request.urlopen(download, timeout=60) as response:
        for block in iter(lambda: response.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    if digest.hexdigest() != update["sha256"] or size != update["size"]:
        raise RuntimeError("Public installer checksum mismatch")
    print(json.dumps({"version": version, "commit": commit, "public_update_verified": True}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", required=True)
    args = parser.parse_args()
    deploy(args.assets, args.version, args.commit)
