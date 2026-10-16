"""Keep completed measurements distinct from approved regression comparisons."""

import json
import math
import os
from pathlib import Path
import statistics

from harness.checks import require

METRICS = ("ack_p95_ms", "ack_p99_ms", "delivery_p95_ms", "delivery_p99_ms", "throughput")
EXIT_CODES = {"COLLECTED": 0, "PASS": 0, "FAIL": 1, "BLOCKED": 2}


def validate_metrics(metrics):
    require(
        all(
            type(metrics.get(key)) in (int, float)
            and math.isfinite(metrics[key])
            and metrics[key] >= 0
            for key in METRICS
        )
        and metrics["throughput"] > 0,
        "Missing or invalid performance measurements",
    )


def validate_baseline(baseline, environment):
    require(
        baseline.get("approved") is True
        and isinstance(baseline.get("approved_by"), str)
        and baseline["approved_by"].strip(),
        "Baseline has no approval evidence",
    )
    require(
        all(
            baseline.get("environment", {}).get(key) == value
            for key, value in environment.items()
            if key != "binary_sha256"
        ),
        "Approved baseline belongs to a different environment/profile",
    )
    validate_metrics(baseline.get("median", {}))


def measurement_report(environment, measurements, baseline=None):
    require(bool(measurements), "No completed performance measurements")
    for item in measurements:
        validate_metrics(item)
        require(not item.get("receiver_errors"), "Load receiver errors present")
    median = {key: statistics.median(item[key] for item in measurements) for key in METRICS}
    status, reason = "COLLECTED", "Measurements collected; candidate baseline is not approved"
    if baseline is not None:
        validate_baseline(baseline, environment)
        require(len(measurements) == 3, "Performance comparison requires three measurements")
        regression = any(
            median[key] > baseline["median"][key] * 1.20
            for key in ("ack_p95_ms", "delivery_p95_ms")
        )
        status, reason = (
            ("FAIL", "P95 regressed more than 20%")
            if regression
            else ("PASS", "P95 within approved 20% threshold")
        )
    return {
        "status": status,
        "reason": reason,
        "comparison_status": "NOT_RUN" if baseline is None else status,
        "environment": environment,
        "median": median,
        "runs": len(measurements),
        "approved": False,
        "measurements": measurements,
    }


def write_report(report, destination):
    destination.write_text(json.dumps(report, indent=2), encoding="utf-8")
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(summary).open("a", encoding="utf-8") as stream:
            stream.write(
                f"### Performance: {report['status']}\n\n"
                f"{report['reason']}\n\n"
                f"Regression comparison: {report.get('comparison_status', 'NOT_RUN')}. "
                "The current measurements are not an approved baseline.\n\n"
            )
