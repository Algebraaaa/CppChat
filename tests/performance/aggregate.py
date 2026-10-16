"""Combine separate two-hour jobs; incompatible hosts cannot form one baseline."""

import argparse
import json
from pathlib import Path
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from performance.reporting import METRICS, validate_metrics, write_report


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    destination = args.directory / "soak-candidate.json"
    reports = [
        json.loads(file.read_text(encoding="utf-8"))
        for file in args.directory.glob("**/performance.json")
    ]
    if len(reports) != 3 or any(
        item.get("status") != "COLLECTED"
        or item.get("runs") != 1
        or item.get("environment") != reports[0].get("environment")
        for item in reports
    ):
        reason = "Require three completed runs on the same environment"
        write_report({"status": "BLOCKED", "reason": reason, "approved": False}, destination)
        print("BLOCKED: " + reason)
        return 2
    result = {
        "status": "COLLECTED",
        "reason": "Three compatible soak measurements collected; candidate baseline is not approved",
        "comparison_status": "NOT_RUN",
        "approved": False,
        "environment": reports[0]["environment"],
        "runs": 3,
        "median": {
            key: statistics.median(item["median"][key] for item in reports) for key in METRICS
        },
    }
    for item in reports:
        validate_metrics(item["median"])
    write_report(result, destination)
    print("COLLECTED: Candidate soak baseline requires separate approval")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
