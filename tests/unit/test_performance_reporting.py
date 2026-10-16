import copy
from contextlib import nullcontext
import json
from pathlib import Path
import subprocess
import sys
from unittest.mock import AsyncMock, Mock

import pytest

from harness.checks import require
from performance import aggregate
from performance import run as load_run
from performance.reporting import EXIT_CODES, measurement_report, validate_baseline, write_report


@pytest.fixture
def environment():
    return {
        "os": "test-os",
        "cpu": "test-cpu",
        "cores": 4,
        "profile": "baseline",
        "connections": 100,
        "duration": 600,
        "binary_sha256": "new-binary",
    }


@pytest.fixture
def measurements():
    return [
        dict(
            ack_p95_ms=100,
            ack_p99_ms=110,
            delivery_p95_ms=100,
            delivery_p99_ms=110,
            throughput=100,
            receiver_errors=[],
        )
        for _ in range(3)
    ]


def approved(environment, measurements):
    report = measurement_report(environment, measurements)
    report.update(approved=True, approved_by="isolated-test-reviewer")
    return report


def test_collection_succeeds_without_claiming_an_approved_comparison(
    environment, measurements, tmp_path, monkeypatch
):
    summary = tmp_path / "summary.md"
    monkeypatch.setenv("GITHUB_STEP_SUMMARY", str(summary))
    result = measurement_report(environment, measurements)
    require(result["status"] == "COLLECTED" and EXIT_CODES[result["status"]] == 0)
    require(result["approved"] is False and result["comparison_status"] == "NOT_RUN")
    write_report(result, tmp_path / "performance.json")
    require(json.loads((tmp_path / "performance.json").read_text())["approved"] is False)
    require("NOT_RUN" in summary.read_text() and "not an approved baseline" in summary.read_text())


@pytest.mark.parametrize("latency,status,exit_code", [(120, "PASS", 0), (120.01, "FAIL", 1)])
def test_comparison_preserves_twenty_percent_gate(
    environment, measurements, latency, status, exit_code
):
    baseline = approved(environment, measurements)
    baseline["environment"] = dict(environment, binary_sha256="old-binary")
    changed = copy.deepcopy(measurements)
    for item in changed:
        item["delivery_p95_ms"] = latency
    result = measurement_report(environment, changed, baseline)
    require(result["status"] == result["comparison_status"] == status)
    require(EXIT_CODES[result["status"]] == exit_code and result["approved"] is False)


@pytest.mark.parametrize("mutation", ["approval", "reviewer", "environment", "median"])
def test_unapproved_or_incompatible_baselines_cannot_pass(environment, measurements, mutation):
    baseline = approved(environment, measurements)
    if mutation == "approval":
        baseline["approved"] = False
    elif mutation == "reviewer":
        baseline["approved_by"] = "  "
    elif mutation == "environment":
        baseline["environment"] = dict(environment, cores=8)
    else:
        baseline["median"]["ack_p95_ms"] = float("nan")
    with pytest.raises(AssertionError):
        validate_baseline(baseline, environment)


@pytest.mark.parametrize(
    "mutation", ["empty", "receiver_error", "nan", "infinity", "missing", "zero_throughput"]
)
def test_collection_does_not_hide_bad_measurements(environment, measurements, mutation):
    if mutation == "empty":
        measurements = []
    elif mutation == "receiver_error":
        measurements[0]["receiver_errors"] = ["Connection lost"]
    elif mutation == "nan":
        measurements[0]["ack_p95_ms"] = float("nan")
    elif mutation == "infinity":
        measurements[0]["delivery_p95_ms"] = float("inf")
    elif mutation == "missing":
        measurements[0].pop("ack_p95_ms")
    else:
        measurements[0]["throughput"] = 0
    with pytest.raises(AssertionError):
        measurement_report(environment, measurements)


def test_comparison_requires_three_runs(environment, measurements):
    with pytest.raises(AssertionError, match="three measurements"):
        measurement_report(environment, measurements[:1], approved(environment, measurements))


def test_compare_without_baseline_exits_before_load_even_under_optimized_python(tmp_path):
    script = Path(__file__).resolve().parents[1] / "performance/run.py"
    for mode in ([], ["-O"]):
        result = subprocess.run(
            [
                sys.executable,
                *mode,
                "-B",
                str(script),
                "--mode",
                "compare",
                "--server-build",
                str(tmp_path / "not-built"),
                "--artifacts",
                str(tmp_path),
            ],
            capture_output=True,
            text=True,
            timeout=20,
        )
        require(result.returncode == 2 and "Compare mode requires" in result.stdout)
        require(json.loads((tmp_path / "performance.json").read_text())["status"] == "BLOCKED")


@pytest.mark.parametrize("k6_exit", [0, 99])
def test_collect_command_preserves_http_load_failure(k6_exit, measurements, tmp_path, monkeypatch):
    monkeypatch.delenv("GITHUB_STEP_SUMMARY", raising=False)
    binary = tmp_path / "isolated-test-binary"
    binary.write_bytes(b"fixture")
    monkeypatch.setattr(load_run, "executable", lambda *args: binary)
    cluster = Mock()
    cluster.ports = {"gate": 8090}

    def isolated_cluster(directory, *args):
        directory.mkdir()
        return nullcontext(cluster)

    monkeypatch.setattr(load_run, "Cluster", isolated_cluster)
    k6 = Mock()
    k6.with_env.return_value = k6
    k6.get_wrapped_container.return_value.wait.return_value = {"StatusCode": k6_exit}
    k6.get_logs.return_value = (b"isolated threshold result", b"")
    monkeypatch.setattr(load_run, "DockerContainer", lambda *args: k6)
    monkeypatch.setattr(load_run, "measure", AsyncMock(return_value=measurements[0]))
    monkeypatch.setattr(
        sys,
        "argv",
        [
            "run.py",
            "--mode",
            "collect",
            "--runs",
            "1",
            "--connections",
            "2",
            "--duration",
            "1",
            "--artifacts",
            str(tmp_path),
        ],
    )
    if k6_exit:
        with pytest.raises(AssertionError, match="HTTP load failed thresholds"):
            load_run.main()
        require(not (tmp_path / "performance.json").exists())
    else:
        require(load_run.main() == 0)
        report = json.loads((tmp_path / "performance.json").read_text())
        require(report["status"] == "COLLECTED" and report["comparison_status"] == "NOT_RUN")


@pytest.mark.parametrize(
    "mutation,code", [(None, 0), ("missing", 2), ("failed", 2), ("environment", 2)]
)
def test_soak_aggregation_keeps_collection_and_comparison_distinct(
    environment, measurements, tmp_path, monkeypatch, mutation, code
):
    monkeypatch.delenv("GITHUB_STEP_SUMMARY", raising=False)
    for i in range(2 if mutation == "missing" else 3):
        path = tmp_path / str(i)
        path.mkdir()
        result = measurement_report(environment, measurements[:1])
        if i == 0 and mutation == "failed":
            result["status"] = "FAIL"
        if i == 0 and mutation == "environment":
            result["environment"] = dict(environment, cores=8)
        (path / "performance.json").write_text(json.dumps(result), encoding="utf-8")
    monkeypatch.setattr(sys, "argv", ["aggregate.py", str(tmp_path)])
    require(aggregate.main() == code)
    result = json.loads((tmp_path / "soak-candidate.json").read_text())
    require(result["status"] == ("COLLECTED" if code == 0 else "BLOCKED"))
    require(result["approved"] is False)
