#!/usr/bin/env python3
"""One regression entry point. No production endpoint/config is accepted."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import importlib.util
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


def cached_tool(build, key, fallback):
    cache = build / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith(key + ":"):
                return line.split("=", 1)[1]
    return shutil.which(fallback) or fallback


def cache_value(build, key):
    cache = build / "CMakeCache.txt"
    if cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith(key + ":"):
                return line.split("=", 1)[1]
    return ""


def execute(name, command, directory, env=None, timeout=1800, cwd=ROOT):
    started = time.monotonic()
    print(f"[{name}] running", flush=True)
    try:
        with (directory / (name + ".log")).open("w", encoding="utf-8") as output:
            result = subprocess.run(
                [str(x) for x in command],
                cwd=cwd,
                env=env,
                stdout=output,
                stderr=subprocess.STDOUT,
                timeout=timeout,
                text=True,
            )
        status = (
            "PASS"
            if result.returncode == 0
            else ("BLOCKED" if name == "performance" and result.returncode == 2 else "FAIL")
        )
        reason = "" if result.returncode == 0 else f"exit {result.returncode}; see {name}.log"
    except FileNotFoundError as error:
        status, reason = "BLOCKED", str(error)
    except subprocess.TimeoutExpired as error:
        status, reason = "FAIL", str(error)
    print(f"[{name}] {status}", flush=True)
    return {
        "name": name,
        "status": status,
        "reason": reason,
        "seconds": round(time.monotonic() - started, 3),
    }


def main():
    sys.dont_write_bytecode = True
    from harness.report import write_report, junit_cases

    # Use the project's isolated environment when the shell defaults to MSYS Python.
    local_python = (
        ROOT / "build/test-env" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    )
    if local_python.exists() and Path(sys.executable).resolve() != local_python.resolve():
        return subprocess.call([str(local_python), str(Path(__file__).resolve()), *sys.argv[1:]])
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=["smoke", "full", "nightly", "release"], default="smoke")
    parser.add_argument("--server-build", type=Path, default=ROOT / "build/server")
    parser.add_argument("--qt-build", type=Path)
    parser.add_argument("--configuration", choices=["Debug", "Release"])
    parser.add_argument("--cmake")
    parser.add_argument("--qt-cmake")
    parser.add_argument(
        "--no-build", action="store_true", help="Use already configured test builds"
    )
    parser.add_argument("--manual-results", type=Path)
    parser.add_argument("--seed", type=int, default=secrets.randbits(32))
    parser.add_argument("--artifacts", type=Path, default=ROOT / "build/regression")
    parser.add_argument(
        "--keep-runs",
        type=int,
        default=3,
        help="Keep N completed reports expanded; archive older runs",
    )
    parser.add_argument(
        "--performance", choices=["baseline", "soak"], help="Run 3 isolated load measurements"
    )
    parser.add_argument("--approved-baseline", type=Path)
    parser.add_argument("--coverage-build", type=Path)
    args = parser.parse_args()
    if args.keep_runs < 1:
        parser.error("--keep-runs must be at least 1")
    args.configuration = args.configuration or ("Release" if args.suite == "release" else "Debug")
    args.qt_build = args.qt_build or ROOT / "CppChat/build" / args.configuration.lower()
    os.chdir(ROOT)
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + secrets.token_hex(4)
    directory = args.artifacts.resolve() / run_id
    directory.mkdir(parents=True)
    stages = []
    if args.coverage_build:
        coverage_root = args.coverage_build.resolve()
        # Each report measures this run, never historical counter files.
        for counter in coverage_root.rglob("*.gcda"):
            if not counter.resolve().is_relative_to(coverage_root):
                raise RuntimeError("Coverage cleanup escaped its build directory")
            counter.unlink()
    env = dict(
        os.environ,
        CPPCHAT_TEST_RUN=run_id,
        CPPCHAT_TEST_ARTIFACTS=str(directory / "screenshots"),
        CPPCHAT_QT_BUILD=str(args.qt_build.resolve()),
        PYTHONHASHSEED=str(args.seed),
        HYPOTHESIS_STORAGE_DIRECTORY=str(directory / "hypothesis"),
        PYTHONDONTWRITEBYTECODE="1",
    )
    if os.name != "nt":
        env.setdefault("QT_QPA_PLATFORM", "offscreen")
    cmake = args.cmake or cached_tool(args.server_build, "CMAKE_COMMAND", "cmake")
    qt_cmake = args.qt_cmake or cached_tool(args.qt_build, "CMAKE_COMMAND", "cmake")
    ctest = cached_tool(args.server_build, "CMAKE_CTEST_COMMAND", "ctest")
    qt_ctest = cached_tool(args.qt_build, "CMAKE_CTEST_COMMAND", "ctest")
    if args.suite == "release":
        debug_server = (
            args.server_build
            if cache_value(args.server_build, "CMAKE_CONFIGURATION_TYPES")
            else Path(str(args.server_build) + "-debug")
        )
        debug_qt = (
            args.qt_build.parent / "debug"
            if args.qt_build.name.lower() == "release"
            else Path(str(args.qt_build) + "-debug")
        )
        if not args.no_build:
            if debug_server != args.server_build:
                stages.append(
                    execute(
                        "release-debug-server-configure",
                        [
                            cmake,
                            "-S",
                            ROOT,
                            "-B",
                            debug_server,
                            "-G",
                            "Ninja",
                            "-DCPPCHAT_BUILD_TESTS=ON",
                            "-DCMAKE_BUILD_TYPE=Debug",
                        ],
                        directory,
                        env,
                    )
                )
            stages.append(
                execute(
                    "release-debug-server-build",
                    [cmake, "--build", debug_server, "--config", "Debug", "--parallel", "4"],
                    directory,
                    env,
                )
            )
            command = [
                qt_cmake,
                "-S",
                ROOT / "CppChat",
                "-B",
                debug_qt,
                "-DCPPCHAT_BUILD_TESTS=ON",
                "-DCMAKE_BUILD_TYPE=Debug",
            ]
            # Reuse the selected Kit when configuring a new single-config directory.
            cache = args.qt_build / "CMakeCache.txt"
            if not (debug_qt / "CMakeCache.txt").exists() and cache.exists():
                for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
                    for key in (
                        "CMAKE_PREFIX_PATH",
                        "CMAKE_CXX_COMPILER",
                        "CMAKE_MAKE_PROGRAM",
                        "Qt6_DIR",
                    ):
                        if line.startswith(key + ":"):
                            command.append("-D" + key + "=" + line.split("=", 1)[1])
                command += ["-G", "Ninja"]
            stages.append(execute("release-debug-qt-configure", command, directory, env))
            stages.append(
                execute(
                    "release-debug-qt-build",
                    [qt_cmake, "--build", debug_qt, "--config", "Debug", "--parallel", "4"],
                    directory,
                    env,
                )
            )
        stages.append(
            execute(
                "release-debug-cpp-tests",
                [
                    ctest,
                    "--test-dir",
                    debug_server,
                    "-C",
                    "Debug",
                    "--no-tests=error",
                    "--output-on-failure",
                    "--output-junit",
                    directory / "debug-cpp-junit.xml",
                ],
                directory,
                env,
            )
        )
        stages.append(
            execute(
                "release-debug-qt-tests",
                [
                    qt_ctest,
                    "--test-dir",
                    debug_qt,
                    "-C",
                    "Debug",
                    "--no-tests=error",
                    "--output-on-failure",
                    "--output-junit",
                    directory / "debug-qt-junit.xml",
                ],
                directory,
                env,
            )
        )
    for name, tool, source, build in (
        ("server", cmake, ROOT, args.server_build),
        ("qt", qt_cmake, ROOT / "CppChat", args.qt_build),
    ):
        if not args.no_build:
            configured = execute(
                name + "-configure",
                [
                    tool,
                    "-S",
                    source,
                    "-B",
                    build,
                    "-DCPPCHAT_BUILD_TESTS=ON",
                    "-DCMAKE_BUILD_TYPE=" + args.configuration,
                ],
                directory,
                env,
            )
            stages.append(configured)
            if configured["status"] == "PASS":
                stages.append(
                    execute(
                        name + "-build",
                        [tool, "--build", build, "--config", args.configuration, "--parallel", "4"],
                        directory,
                        env,
                    )
                )
    for name, build in (("server", args.server_build), ("qt", args.qt_build)):
        actual = cache_value(build, "CMAKE_BUILD_TYPE")
        if not cache_value(build, "CMAKE_CONFIGURATION_TYPES") and actual != args.configuration:
            stages.append(
                {
                    "name": name + "-configuration",
                    "status": "BLOCKED",
                    "reason": f"Requested {args.configuration}, actual single-config build is {actual or 'missing'}",
                }
            )
    for previous in (args.server_build / "core-junit.xml", args.qt_build / "client-junit.xml"):
        if previous.is_file():
            previous.unlink()
    stages.append(
        execute(
            "cpp-tests",
            [
                ctest,
                "--test-dir",
                args.server_build,
                "-C",
                args.configuration,
                "--no-tests=error",
                "--output-on-failure",
                "--output-junit",
                directory / "cpp-ctest-junit.xml",
            ],
            directory,
            env,
        )
    )
    stages.append(
        execute(
            "qt-tests",
            [
                qt_ctest,
                "--test-dir",
                args.qt_build,
                "-C",
                args.configuration,
                "--no-tests=error",
                "--output-on-failure",
                "--output-junit",
                directory / "qt-ctest-junit.xml",
            ],
            directory,
            env,
        )
    )
    for source, name in (
        (args.server_build / "core-junit.xml", "cpp-junit.xml"),
        (args.qt_build / "client-junit.xml", "qt-junit.xml"),
    ):
        if source.exists():
            shutil.copy2(source, directory / name)
    stages.append(
        execute(
            "verify-tests",
            [
                shutil.which("node") or "node",
                "--test",
                "--test-reporter=junit",
                "--test-reporter-destination=" + str(directory / "verify-junit.xml"),
                str(ROOT / "tests/node/verify.test.cjs"),
            ],
            directory,
            env,
        )
    )
    if importlib.util.find_spec("ruff") is None:
        stages.append(
            {
                "name": "test-style",
                "status": "BLOCKED",
                "reason": "Install the locked ruff dependency",
            }
        )
    else:
        stages.append(
            execute(
                "test-style",
                [
                    sys.executable,
                    "-m",
                    "ruff",
                    "check",
                    "--cache-dir",
                    ROOT / "build/test-cache/ruff",
                    "tests",
                ],
                directory,
                env,
            )
        )
        stages.append(
            execute(
                "test-format",
                [
                    sys.executable,
                    "-m",
                    "ruff",
                    "format",
                    "--check",
                    "--cache-dir",
                    ROOT / "build/test-cache/ruff",
                    "tests",
                ],
                directory,
                env,
            )
        )
    missing = [
        module
        for module in (
            "pytest",
            "pytest_html",
            "testcontainers",
            "pymysql",
            "redis",
            "requests",
            "hypothesis",
            "cryptography",
            "psutil",
        )
        if importlib.util.find_spec(module) is None
    ]
    if missing:
        stages.append(
            {
                "name": "python-tests",
                "status": "BLOCKED",
                "reason": "Install tests/requirements.lock in a venv: " + ", ".join(missing),
            }
        )
    else:
        expression = {
            "smoke": "smoke",
            "full": "not nightly and not release",
            "nightly": "not release",
            "release": "",
        }[args.suite]

        def run_mode(optimized):
            mode = "optimized" if optimized else "normal"
            output = directory / mode
            output.mkdir()
            command = (
                [sys.executable]
                + (["-O"] if optimized else [])
                + [
                    "-m",
                    "pytest",
                    "-c",
                    ROOT / "tests/pytest.ini",
                    ROOT / "tests/unit",
                    ROOT / "tests/integration",
                    ROOT / "tests/regression",
                    "-m",
                    expression,
                    "--server-build",
                    args.server_build.resolve(),
                    "--configuration",
                    args.configuration,
                    "--artifacts",
                    output,
                    "--junitxml",
                    output / "pytest-junit.xml",
                    "--html",
                    output / "pytest.html",
                    "--self-contained-html",
                    "-o",
                    "cache_dir=" + str(output / "pytest-cache"),
                    "--hypothesis-seed",
                    str(args.seed),
                ]
            )
            if args.manual_results:
                command += ["--manual-results", args.manual_results.resolve()]
            result = execute("pytest-" + mode, command, directory, env, timeout=3600)
            cases = junit_cases(output / "pytest-junit.xml")
            if not cases and result["status"] == "PASS":
                result.update(status="FAIL", reason="No tests collected")
            if any(case["status"] == "BLOCKED" for case in cases) and result["status"] == "PASS":
                result.update(status="BLOCKED", reason="Required tests skipped; inspect JUnit")
            return result

        with ThreadPoolExecutor(max_workers=2) as workers:
            futures = [workers.submit(run_mode, mode) for mode in (False, True)]
            try:
                stages.extend(future.result() for future in futures)
            finally:
                from harness.cleanup import reclaim

                leaked = reclaim(directory, run_id)
                stages.append(
                    {
                        "name": "cleanup-audit",
                        "status": "FAIL" if leaked else "PASS",
                        "reason": "Reclaimed leaked owned resources: " + ", ".join(leaked)
                        if leaked
                        else "All owned resources reclaimed by fixtures",
                    }
                )
    if args.performance:
        command = [
            sys.executable,
            ROOT / "tests/performance/run.py",
            "--profile",
            args.performance,
            "--server-build",
            args.server_build,
            "--configuration",
            args.configuration,
            "--artifacts",
            directory / "performance",
        ]
        if args.approved_baseline:
            command += ["--approved-baseline", args.approved_baseline]
        stages.append(
            execute(
                "performance",
                command,
                directory,
                env,
                timeout=23000 if args.performance == "soak" else 2400,
            )
        )
    if args.coverage_build:
        coverage_work = directory / "coverage-work"
        coverage_work.mkdir()
        stages.append(
            execute(
                "coverage-raw",
                [
                    sys.executable,
                    "-m",
                    "gcovr",
                    "--root",
                    ROOT,
                    "--object-directory",
                    args.coverage_build.resolve(),
                    "--filter",
                    re.escape(ROOT.as_posix() + "/ChatServerShared/"),
                    "--filter",
                    re.escape(ROOT.as_posix() + "/GateServer/"),
                    "--exclude",
                    ".*message\\.(grpc\\.)?pb\\..*",
                    "--xml",
                    directory / "coverage-raw.xml",
                    "--html-details",
                    directory / "coverage-raw.html",
                    args.coverage_build.resolve(),
                ],
                directory,
                env,
                cwd=coverage_work,
            )
        )
        stages.append(
            execute(
                "coverage",
                [
                    sys.executable,
                    "-m",
                    "gcovr",
                    "--root",
                    ROOT,
                    "--object-directory",
                    args.coverage_build.resolve(),
                    "--filter",
                    re.escape(ROOT.as_posix() + "/ChatServerShared/"),
                    "--filter",
                    re.escape(ROOT.as_posix() + "/GateServer/"),
                    "--exclude",
                    ".*message\\.(grpc\\.)?pb\\..*",
                    "--exclude-throw-branches",
                    "--exclude-unreachable-branches",
                    "--fail-under-line",
                    "80",
                    "--fail-under-branch",
                    "70",
                    "--xml",
                    directory / "coverage.xml",
                    "--html-details",
                    directory / "coverage.html",
                    args.coverage_build.resolve(),
                ],
                directory,
                env,
                cwd=coverage_work,
            )
        )
    if args.suite == "release" and args.configuration != "Release":
        stages.append(
            {
                "name": "release-build",
                "status": "BLOCKED",
                "reason": "Repeat with --configuration Release and a Release Qt build",
            }
        )
    metadata = {
        "suite": args.suite,
        "run_id": run_id,
        "seed": args.seed,
        "platform": platform.platform(),
        "python": sys.version,
        "configuration": args.configuration,
        "manual_checklist": "tests/manual/windows.md",
        "tools": {"cmake": cmake, "qt_cmake": qt_cmake},
        "dependency_lock": "tests/requirements.lock",
        "implementation_status": {"收藏": "未实现", "发现": "未实现", "语音/视频": "未实现"},
        "pending_acceptance": []
        if args.manual_results
        else ["Windows 桌面、多屏/DPI、系统剪贴板、发布包"],
        "coverage": "未运行" if not args.coverage_build else "见 coverage 阶段",
        "coverage_branch_policy": "Conditions with compiler exception/unreachable arcs excluded; explicit exception handling remains covered by fault tests",
    }
    state = write_report(directory, stages, metadata)
    from harness.cleanup import archive_completed_runs

    try:
        archived = archive_completed_runs(args.artifacts, args.keep_runs)
        if archived:
            print(
                f"Archived {len(archived)} older reports in {args.artifacts / 'archive'}",
                flush=True,
            )
    except (OSError, ValueError) as error:
        # Report retention must not conceal the functional result or delete evidence.
        print(f"Report archive incomplete: {error}", flush=True)
    print(f"{state}: {directory / 'index.html'}", flush=True)
    return {"PASS": 0, "FAIL": 1, "BLOCKED": 2}[state]


if __name__ == "__main__":
    sys.exit(main())
