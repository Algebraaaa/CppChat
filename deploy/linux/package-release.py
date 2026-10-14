"""Export the tested Linux build and canonical Git source for CD."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

from release_bundle import BINARIES, COMMIT, VERSION, sha256


def package(build, version, output):
    root = Path(__file__).resolve().parents[2]
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root).decode().strip()
    if not VERSION.fullmatch(version) or not COMMIT.fullmatch(commit):
        raise ValueError("Invalid release identity")
    if subprocess.check_output(
        ["git", "status", "--porcelain", "--untracked-files=no"], cwd=root
    ).strip():
        raise ValueError("Release requires an unchanged Git checkout")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    stage = root / "build/release-linux" / version
    stage.mkdir(parents=True, exist_ok=False)
    (stage / "bin").mkdir()
    for name in BINARIES:
        binary = build / "bin" / name
        shutil.copy2(binary, stage / "bin" / name)
    for folder in ("VerifyServer", "GatewayGuard"):
        (stage / folder).mkdir()
        for name in (
            subprocess.check_output(["git", "ls-files", folder], cwd=root).decode().splitlines()
        ):
            path = Path(name)
            if path.suffix in (".js", ".proto") or path.name in (
                "package.json",
                "package-lock.json",
            ):
                shutil.copy2(root / path, stage / path)
    subprocess.run(
        [
            "git",
            "-c",
            "core.autocrlf=false",
            "archive",
            "--format=tar.gz",
            "-o",
            str(stage / "source.tar.gz"),
            commit,
        ],
        cwd=root,
        check=True,
    )
    manifest = {
        "version": version,
        "commit": commit,
        "platform": "ubuntu-24.04-x86_64",
        "files": {
            p.relative_to(stage).as_posix(): sha256(p) for p in stage.rglob("*") if p.is_file()
        },
    }
    (stage / "manifest.json").write_text(json.dumps(manifest, indent=2))
    target = output / f"CppChat-Server-{version}-linux-x64.tar.gz"
    if target.exists():
        raise ValueError("Refusing to overwrite a release artifact")
    with tarfile.open(target, "w:gz") as archive:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                archive.add(path, arcname=path.relative_to(stage).as_posix(), recursive=False)
    (output / (target.name + ".sha256")).write_text(sha256(target) + "  " + target.name + "\n")
    print(
        json.dumps(
            {
                "version": version,
                "commit": commit,
                "artifact": target.name,
                "sha256": sha256(target),
            }
        )
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, default=Path("build/packages"))
    args = parser.parse_args()
    package(args.build, args.version, args.output)
