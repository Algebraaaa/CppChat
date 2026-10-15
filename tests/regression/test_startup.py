import os
from pathlib import Path
import subprocess
import pytest
from harness.checks import require
from harness.cluster import qt_environment


@pytest.mark.feature("BOOT-PACKAGE")
@pytest.mark.parametrize("mode", ["help", "missing", "invalid"])
def test_client_startup_configuration_and_exit(tmp_path, mode):
    build = Path(os.environ.get("CPPCHAT_QT_BUILD", "CppChat/build/debug"))
    binary = build / ("CppChat.exe" if os.name == "nt" else "CppChat")
    if not binary.exists():
        pytest.skip("BLOCKED: Client package not built")
    data, logs = tmp_path / "data", tmp_path / "logs"
    configuration = tmp_path / "config.ini"
    if mode == "invalid":
        # Keep transport valid so this case specifically exercises the invalid port,
        # including Release builds that reject insecure transport with a modal dialog.
        configuration.write_text(
            "[GateServer]\nhost=127.0.0.1\nport=70000\nscheme=https\n"
            "[ChatServer]\ntls=true\n[ResourceServer]\ntls=true\n",
            encoding="utf-8",
        )
    command = (
        [str(binary.resolve()), "--help"]
        if mode == "help"
        else [
            str(binary.resolve()),
            "--config",
            str(configuration),
            "--data-dir",
            str(data),
            "--log-dir",
            str(logs),
        ]
    )
    response = subprocess.run(
        command, cwd=tmp_path, env=qt_environment(build), capture_output=True, timeout=15
    )
    require(
        response.returncode == (0 if mode == "help" else 1),
        f"Unexpected client startup exit: {response.returncode}",
    )
    require(not data.exists(), "Failed startup created account data")
