import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def fingerprint():
    paths = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT
    ).split(b"\0")
    digest = hashlib.sha256()
    for entry in sorted(set(paths)):
        if not entry:
            continue
        path = ROOT / entry.decode("utf-8")
        if path.is_file():
            digest.update(entry)
            digest.update(path.read_bytes())
    return digest.hexdigest()


def template():
    catalog = json.loads((ROOT / "tests/cases.json").read_text(encoding="utf-8"))
    return {
        "source_fingerprint": fingerprint(),
        "platform": "Windows",
        "configuration": "Release",
        "qt_kit": "",
        "compiler": "",
        "accepted_by": "",
        "accepted_at": "",
        "display_layout": "",
        "dpi": [100, 125, 150, 200],
        "cases": {
            item["id"]: {"status": "BLOCKED", "evidence": [], "notes": "待验收"}
            for item in catalog
            if item["kind"] == "manual"
        },
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.output.exists():
        raise SystemExit("Refusing to overwrite an existing acceptance record")
    args.output.write_text(json.dumps(template(), ensure_ascii=False, indent=2), encoding="utf-8")
