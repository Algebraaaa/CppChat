"""Reclaim owned resources and archive completed regression artifacts."""

import json
from pathlib import Path
import re
import shutil
from zipfile import ZIP_DEFLATED, ZipFile


def archive_completed_runs(directory, keep=3):
    """Keep recent reports expanded; preserve older evidence in verified ZIPs.

    Only directories carrying the runner's matching completed summary are owned.
    Unknown directories, unfinished runs and links are never removed.
    """
    if keep < 1:
        raise ValueError("At least one completed run must remain expanded")
    location = Path(directory)
    if location.is_symlink() or location.is_junction():
        raise ValueError("Artifact root must not be a link")
    location = location.resolve(strict=True)
    completed = []
    for run in location.iterdir():
        if not re.fullmatch(r"\d{8}T\d{6}Z-[0-9a-f]{8}", run.name):
            continue
        if not run.is_dir() or run.is_symlink() or run.is_junction():
            continue
        summary = run / "summary.json"
        if not summary.is_file() or summary.is_symlink():
            continue
        try:
            metadata = json.loads(summary.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        if (
            isinstance(metadata, dict)
            and metadata.get("run_id") == run.name
            and metadata.get("status")
            in (
                "PASS",
                "FAIL",
                "BLOCKED",
            )
        ):
            completed.append(run)
    archived = []
    for run in sorted(completed, key=lambda path: path.name, reverse=True)[keep:]:
        entries = list(run.rglob("*"))
        if any(
            item.is_symlink()
            or item.is_junction()
            or not item.resolve().is_relative_to(run.resolve())
            for item in entries
        ):
            continue
        archive_root = location / "archive"
        if archive_root.is_symlink() or archive_root.is_junction():
            raise ValueError("Archive directory must not be a link")
        archive_root.mkdir(exist_ok=True)
        target = archive_root / (run.name + ".zip")
        temporary = archive_root / (run.name + ".zip.tmp")
        if target.exists():
            raise FileExistsError(f"Archive already exists: {target}")
        try:
            with ZipFile(temporary, "x", compression=ZIP_DEFLATED) as archive:
                for item in entries:
                    if item.is_file():
                        archive.write(item, item.relative_to(location).as_posix())
            with ZipFile(temporary) as archive:
                if archive.testzip() is not None:
                    raise OSError(f"Archive verification failed: {temporary}")
            temporary.replace(target)
            if run.parent != location or not run.resolve().is_relative_to(location):
                raise ValueError("Cleanup escaped artifact root")
            shutil.rmtree(run)
            archived.append(target)
        finally:
            temporary.unlink(missing_ok=True)
    return archived


def reclaim(directory, run_id):
    import psutil
    import docker

    leaked = []
    for manifest in Path(directory).rglob("owned-processes.json"):
        for owned in json.loads(manifest.read_text(encoding="utf-8")):
            try:
                process = psutil.Process(owned["pid"])
                if abs(process.create_time() - owned["created"]) > 0.01:
                    continue  # PID was reused by an unrelated process.
                leaked.append("process:" + owned["name"])
                process.terminate()
                try:
                    process.wait(timeout=10)
                except psutil.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            except psutil.NoSuchProcess:
                pass
    client = None
    try:
        client = docker.from_env()
        for container in client.containers.list(
            all=True, filters={"label": "cppchat.regression.run=" + run_id}
        ):
            leaked.append("container:" + container.short_id)
            container.remove(force=True, v=True)
    except docker.errors.DockerException:
        pass  # Missing Docker is already a BLOCKED environment, not a pass.
    finally:
        if client is not None:
            client.close()
    return leaked
