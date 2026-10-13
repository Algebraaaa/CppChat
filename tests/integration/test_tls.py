import os
from pathlib import Path
import subprocess
import pytest
import requests
from harness.checks import require
from harness.tls import TLSProxy, certificates
from harness.cluster import qt_environment

pytestmark = [pytest.mark.integration, pytest.mark.feature("TLS-CERTIFICATE")]


@pytest.mark.parametrize("mode", ["valid", "hostname", "expired", "untrusted"])
@pytest.mark.parametrize("transport", ["tcp", "http"])
def test_tls_certificate_and_hostname_validation(cluster, tmp_path, mode, transport):
    ca, certificate, key = certificates(
        tmp_path,
        expired=mode == "expired",
        untrusted=mode == "untrusted",
        hostname_mismatch=mode == "hostname",
    )
    with TLSProxy(certificate, key, ("127.0.0.1", cluster.ports["gate"])) as proxy:
        if mode == "valid":
            response = requests.get(
                f"https://127.0.0.1:{proxy.port}/get_test", verify=str(ca), timeout=5
            )
            require(response.ok, "TLS loopback HTTP proxy failed")
        elif mode != "hostname":
            with pytest.raises(requests.exceptions.SSLError):
                requests.get(f"https://127.0.0.1:{proxy.port}/get_test", verify=str(ca), timeout=5)
        build = Path(os.environ.get("CPPCHAT_QT_BUILD", "CppChat/build/debug"))
        binary = build / "tests/qt" / ("tls_probe.exe" if os.name == "nt" else "tls_probe")
        if not binary.exists():
            pytest.skip("BLOCKED: Build Qt tls_probe with CPPCHAT_BUILD_TESTS")
        result = subprocess.run(
            [
                str(binary.resolve()),
                str(proxy.port),
                str(ca),
                "wrong.example.test" if mode == "hostname" else "localhost",
                "valid" if mode == "valid" else "reject",
                transport,
            ],
            capture_output=True,
            timeout=15,
            env=qt_environment(build),
        )
        if result.returncode == 2:
            pytest.skip("BLOCKED: Qt TLS provider unavailable or connection failed")
        require(result.returncode == 0, f"Qt accepted/rejected certificate incorrectly: {mode}")
