"""Real native logger processes; no server config, network, or business data."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class LoggingTests(unittest.TestCase):
    def setUp(self):
        self.sandbox = tempfile.TemporaryDirectory(prefix="logging-", dir=ARTIFACTS)
        self.addCleanup(self.sandbox.cleanup)
        self.directory = Path(self.sandbox.name) / "日志"
        self.env = {
            key: value for key, value in os.environ.items()
            if not key.startswith("CPPCHAT_LOG_") and key != "JOURNAL_STREAM"
        }

    def tearDown(self):
        result = self._outcome.result
        if any(test is self for test, _ in result.failures + result.errors):
            shutil.copytree(self.sandbox.name, ARTIFACTS / ("failed-" + self._testMethodName),
                            dirs_exist_ok=True)

    def run_probe(self, mode="write", **settings):
        result = subprocess.run(
            [str(PROBE), mode, str(self.directory)], env={**self.env, **settings},
            capture_output=True, text=True, encoding="utf-8", timeout=25,
        )
        (Path(self.sandbox.name) / (mode + "-stdout.log")).write_text(
            result.stdout, encoding="utf-8")
        (Path(self.sandbox.name) / (mode + "-stderr.log")).write_text(
            result.stderr, encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def text(self):
        return "\n".join(path.read_text(encoding="utf-8")
                         for path in self.directory.rglob("*.log"))

    def test_default_filter_literal_text_and_no_duplicate_console(self):
        result = self.run_probe()
        text = self.text()
        self.assertNotIn("DEBUG_RECORD", text)
        self.assertNotIn("FILTERED_RECORD", text)
        for message in ["INFO_RECORD {literal braces}", "WARNING_RECORD", "ERROR_RECORD"]:
            self.assertEqual(text.count(message), 1)
            self.assertNotIn(message, result.stdout + result.stderr)
        self.assertRegex(text, r"\[pid \d+\]")

    def test_debug_and_invalid_environment(self):
        self.run_probe(CPPCHAT_LOG_LEVEL="debug")
        self.assertIn("DEBUG_RECORD", self.text())
        result = self.run_probe(CPPCHAT_LOG_LEVEL="bogus", CPPCHAT_LOG_MAX_MB="-1",
                                CPPCHAT_LOG_BACKUPS="999", CPPCHAT_LOG_INSTANCE="../unsafe")
        self.assertIn("Invalid CPPCHAT_LOG_LEVEL", result.stderr)
        self.assertIn("Invalid CPPCHAT_LOG_MAX_MB", result.stderr)
        self.assertIn("Invalid CPPCHAT_LOG_BACKUPS", result.stderr)
        self.assertIn("Invalid CPPCHAT_LOG_INSTANCE", result.stderr)

    def test_console_mode(self):
        result = self.run_probe(CPPCHAT_LOG_OUTPUT="console")
        self.assertIn("WARNING_RECORD", result.stdout)
        self.assertFalse(self.directory.exists())

    def test_journal_environment_does_not_duplicate_file_output(self):
        result = self.run_probe(JOURNAL_STREAM="1:2")
        self.assertIn("WARNING_RECORD", self.text())
        self.assertNotIn("WARNING_RECORD", result.stdout + result.stderr)

    def test_rotation_is_bounded_and_restarts_reuse_the_slot(self):
        for _ in range(3):
            self.run_probe("rotate")
        files = list(self.directory.rglob("*.log"))
        self.assertEqual(len(files), 3)
        self.assertTrue(all(path.stat().st_size <= 1024 for path in files))
        self.assertEqual(len(list(self.directory.rglob(".owner.lock"))), 1)
        self.assertIn("ROTATE-99-", self.text())

    def test_threads_and_exit_drain(self):
        self.run_probe("threaded")
        text = self.text()
        records = re.findall(r"RECORD-(\d+)-(\d+)$", text, re.MULTILINE)
        self.assertEqual(len(records), 800)
        self.assertEqual(len(set(records)), 800)
        self.assertEqual(text.count("IMPORTANT_END"), 1)

    def test_overflow_reports_loss_and_preserves_errors(self):
        self.run_probe("overflow")
        text = self.text()
        self.assertIn("routine messages dropped", text)
        self.assertIn("IMPORTANT_SURVIVES", text)

    def test_unwritable_file_target_falls_back_to_console(self):
        self.directory.parent.mkdir(parents=True, exist_ok=True)
        self.directory.write_text("regular file", encoding="utf-8")
        result = self.run_probe()
        self.assertIn("using console only", result.stderr)
        self.assertIn("ERROR_RECORD", result.stdout)

    def test_simultaneous_processes_and_crash_release(self):
        processes = []
        try:
            paths = []
            for _ in range(2):
                process = subprocess.Popen(
                    [str(PROBE), "hold", str(self.directory)], env=self.env,
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    text=True, encoding="utf-8",
                )
                processes.append(process)
                paths.append(process.stdout.readline().strip())
            self.assertTrue(all(path.startswith("PATH=") for path in paths))
            self.assertNotEqual(paths[0], paths[1])
            processes[0].kill()
            processes[0].communicate(timeout=5)
            result = self.run_probe()
            self.assertIn(paths[0], result.stdout)
            processes[1].communicate("stop\n", timeout=5)
            self.assertEqual(processes[1].returncode, 0)
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)

    def test_metrics_idle_deltas_rate_limit_and_shutdown(self):
        self.run_probe("metrics")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path, required=True)
    args = parser.parse_args()
    PROBE = args.probe.resolve()
    ARTIFACTS = args.artifacts.resolve()
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    unittest.main(argv=[__file__], verbosity=2)
