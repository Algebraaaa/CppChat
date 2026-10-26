"""Run real QML layout and interaction checks in isolated host processes."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmltestrunner", default="qmltestrunner")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = root / "build/android-ui"
    (output / "screenshots").mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, QT_QUICK_CONTROLS_STYLE="Material")
    runner = Path(args.qmltestrunner)
    if runner.is_file():
        host = runner.resolve().parent.parent
        env["PATH"] = str(host / "bin") + os.pathsep + env.get("PATH", "")
        # Cross-compilation CI exports Android plugin paths. Host probes must
        # load the matching host Kit, not try ARM64 plugins in a desktop process.
        env["QT_PLUGIN_PATH"] = str(host / "plugins")
        env["QML_IMPORT_PATH"] = str(host / "qml")
        env["QML2_IMPORT_PATH"] = str(host / "qml")
        if sys.platform.startswith("linux"):
            env["LD_LIBRARY_PATH"] = str(host / "lib") + os.pathsep + env.get("LD_LIBRARY_PATH", "")
    common = [args.qmltestrunner, "-input", str(root / "CppChatAndroid/tests/qml"),
              "-platform", "offscreen"]
    source = (root / "CppChatAndroid/tests/qml/tst_chatui.qml").read_text(encoding="utf-8")
    names = ["AndroidChatUi::" + name for name in re.findall(r"function\s+(test_\w+)\s*\(", source)
             if not name.endswith("_data")]
    groups = {"snapshots": [name for name in names if name.endswith("test_visualSnapshots")],
              "interaction": [name for name in names if not name.endswith("test_visualSnapshots")]}
    results = {}
    started = time.time()
    for name, cases in groups.items():
        if not cases:
            raise RuntimeError("Missing QML test group: " + name)
        command = common + cases + ["-o", str(output / (name + ".txt")) + ",txt",
                                    "-o", str(output / (name + ".xml")) + ",junitxml"]
        try:
            result = subprocess.run(command, cwd=root, env=env, capture_output=True,
                                    text=True, encoding="utf-8", timeout=30)
            code = result.returncode
            (output / (name + "-process.log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        except subprocess.TimeoutExpired:
            code = 124
        results[name] = {"exit_code": code, "functions": cases}
        print(name + ": " + ("PASS" if code == 0 else "FAIL (exit " + str(code) + ")"))
    # A missing or empty capture must not silently count as visual validation.
    images = [output / "screenshots" / (kind + "-" + size + ".png")
              for kind in ("chat", "list") for size in ("narrow", "phone", "landscape")]
    images_ok = all(path.is_file() and path.stat().st_size > 0 and path.stat().st_mtime >= started
                    for path in images)
    results["screenshots_present"] = images_ok
    (output / "summary.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    return 0 if images_ok and all(result["exit_code"] == 0 for result in results.values()
                                if isinstance(result, dict)) else 1


if __name__ == "__main__":
    sys.exit(main())
