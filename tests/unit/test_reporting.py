import subprocess
import sys
import os
import json
from pathlib import Path
from zipfile import ZipFile
import xml.etree.ElementTree as ET
import pytest
from harness.checks import require
from harness.cleanup import archive_completed_runs
from harness.report import junit_cases, write_report


def test_optimized_python_cannot_disable_check():
    result = subprocess.run(
        [
            sys.executable,
            "-O",
            "-c",
            "from harness.checks import require; require(False, 'sentinel')",
        ],
        capture_output=True,
        text=True,
        env=dict(os.environ, PYTHONPATH=str(Path(__file__).resolve().parents[1])),
    )
    require(
        result.returncode != 0 and "sentinel" in result.stderr,
        "python -O disabled a required check",
    )


def test_skips_and_missing_environment_are_not_pass(tmp_path):
    root = ET.Element("testsuite")
    ET.SubElement(root, "testcase", name="good")
    skipped = ET.SubElement(root, "testcase", name="docker")
    ET.SubElement(skipped, "skipped", message="BLOCKED: engine unavailable")
    ET.ElementTree(root).write(tmp_path / "fixture-junit.xml")
    statuses = [row["status"] for row in junit_cases(tmp_path / "fixture-junit.xml")]
    require(statuses == ["PASS", "BLOCKED"])
    require(
        write_report(tmp_path, [], {"suite": "smoke", "run_id": "self-test", "seed": 1})
        == "BLOCKED"
    )


def test_failed_case_dominates_summary(tmp_path):
    root = ET.Element("testsuite")
    failure = ET.SubElement(root, "testcase", name="failure")
    ET.SubElement(failure, "failure", message="Message lost")
    ET.ElementTree(root).write(tmp_path / "fixture-junit.xml")
    require(
        write_report(
            tmp_path,
            [{"name": "manual", "status": "BLOCKED"}],
            {"suite": "full", "run_id": "self-test", "seed": 2},
        )
        == "FAIL"
    )


def test_retention_preserves_failed_evidence_and_unknown_directories(tmp_path):
    for position, status in enumerate(("FAIL", "PASS", "BLOCKED", "PASS")):
        run_id = f"20261012T10000{position}Z-1234abcd"
        run = tmp_path / run_id
        run.mkdir()
        (run / "summary.json").write_text(
            json.dumps({"run_id": run_id, "status": status}), encoding="utf-8"
        )
        (run / "failure.log").write_bytes(b"evidence\x00")
    unknown = tmp_path / "20261012T090000Z-1234abcd"
    unknown.mkdir()
    (unknown / "user-data.txt").write_text("keep", encoding="utf-8")
    archived = archive_completed_runs(tmp_path, keep=3)
    require(len(archived) == 1)
    with ZipFile(archived[0]) as archive:
        require(archive.read("20261012T100000Z-1234abcd/failure.log") == b"evidence\x00")
        require(
            json.loads(archive.read("20261012T100000Z-1234abcd/summary.json"))["status"] == "FAIL"
        )
    require(not (tmp_path / "20261012T100000Z-1234abcd").exists())
    require(unknown.is_dir())
    require((tmp_path / "20261012T100001Z-1234abcd/failure.log").is_file())


def test_archive_verification_failure_keeps_original(tmp_path, monkeypatch):
    for position in range(2):
        run_id = f"20261012T10000{position}Z-1234abcd"
        run = tmp_path / run_id
        run.mkdir()
        (run / "summary.json").write_text(
            json.dumps({"run_id": run_id, "status": "FAIL"}), encoding="utf-8"
        )
    monkeypatch.setattr(ZipFile, "testzip", lambda self: "damaged.log")
    with pytest.raises(OSError, match="verification failed"):
        archive_completed_runs(tmp_path, keep=1)
    require((tmp_path / "20261012T100000Z-1234abcd/summary.json").is_file())
    require(not list((tmp_path / "archive").iterdir()))
