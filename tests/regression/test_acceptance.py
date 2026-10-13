import json
from pathlib import Path
import pytest
from harness.cluster import ROOT
from harness.checks import require
from manual.prepare import fingerprint

CASES = [
    row
    for row in json.loads((ROOT / "tests/cases.json").read_text(encoding="utf-8"))
    if row["kind"] == "manual"
]


@pytest.mark.release
@pytest.mark.manual
@pytest.mark.parametrize("case", CASES, ids=lambda row: row["id"])
def test_recorded_windows_acceptance(request, case):
    path = request.config.getoption("--manual-results")
    if not path:
        pytest.skip("BLOCKED: Windows desktop acceptance missing: " + case["id"])
    record = json.loads(Path(path).read_text(encoding="utf-8"))
    require(
        record.get("source_fingerprint") == fingerprint(),
        "Acceptance belongs to different source files",
    )
    require(record.get("platform") == "Windows" and record.get("configuration") == "Release")
    require(
        all(
            record.get(field)
            for field in ("accepted_by", "accepted_at", "qt_kit", "compiler", "display_layout")
        ),
        "Acceptance metadata missing",
    )
    require({100, 125, 150, 200} <= set(record.get("dpi", [])), "DPI acceptance incomplete")
    entry = record.get("cases", {}).get(case["id"], {})
    if entry.get("status") == "BLOCKED":
        pytest.skip("BLOCKED: " + case["id"])
    require(entry.get("status") == "PASS", "Manual acceptance failed/missing: " + case["id"])
    require(entry.get("evidence"), "No acceptance evidence: " + case["id"])
    for artifact in entry["evidence"]:
        evidence = Path(artifact)
        if not evidence.is_absolute():
            evidence = Path(path).resolve().parent / evidence
        require(evidence.is_file() and evidence.stat().st_size > 0, "Acceptance evidence missing")
