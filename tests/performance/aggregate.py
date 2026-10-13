"""Combine separate two-hour jobs; incompatible hosts cannot form one baseline."""

import argparse
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    reports = [
        json.loads(file.read_text(encoding="utf-8"))
        for file in args.directory.glob("**/performance.json")
    ]
    if len(reports) != 3 or any(
        item["environment"] != reports[0]["environment"] for item in reports
    ):
        print("BLOCKED: Require three completed runs on the same environment")
        return 2
    result = {
        "status": "BLOCKED",
        "reason": "Candidate soak baseline requires approval",
        "approved": False,
        "environment": reports[0]["environment"],
        "runs": 3,
        "median": {
            key: statistics.median(item["median"][key] for item in reports)
            for key in reports[0]["median"]
        },
    }
    (args.directory / "soak-candidate.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8"
    )
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
