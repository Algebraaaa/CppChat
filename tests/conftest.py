import os
from pathlib import Path
import pytest
import uuid

from harness.cluster import Cluster, redact


def pytest_addoption(parser):
    parser.addoption(
        "--server-build", default=os.environ.get("CPPCHAT_SERVER_BUILD", "build/server")
    )
    parser.addoption("--configuration", default="Debug")
    parser.addoption("--artifacts", default="build/regression/pytest")
    parser.addoption("--manual-results", default=None)


@pytest.fixture(scope="session")
def docker_available():
    import docker

    try:
        client = docker.from_env()
        try:
            client.ping()
        finally:
            client.close()
    except Exception as error:
        pytest.skip(f"BLOCKED: Docker engine unavailable: {type(error).__name__}")


@pytest.fixture(scope="session")
def cluster(request, docker_available):
    directory = Path(request.config.getoption("--artifacts")).resolve() / "cluster"
    try:
        with Cluster(
            directory,
            request.config.getoption("--server-build"),
            request.config.getoption("--configuration"),
        ) as services:
            yield services
    except FileNotFoundError as error:
        pytest.skip(f"BLOCKED: {error}")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    if report.failed:
        services = item.funcargs.get("cluster") or item.funcargs.get("recovery_cluster")
        credentials = services.credentials if services else []
        report.longrepr = redact(str(report.longrepr), credentials)
        report.sections = [(name, redact(text, credentials)) for name, text in report.sections]
    for mark in item.iter_markers("feature"):
        report.user_properties.append(("feature", mark.args[0]))


@pytest.fixture
def pair(cluster):
    first, second = cluster.register(), cluster.register()
    a, b = cluster.login(first, "chat1"), cluster.login(second, "chat2")
    thread = cluster.friends(first, second, a, b)
    yield first, second, a, b, thread
    a.close()
    b.close()


@pytest.fixture
def recovery_cluster(request, docker_available):
    # Destructive dependency faults get their own services and pools, not just users.
    directory = Path(request.config.getoption("--artifacts")).resolve() / (
        "fault-" + uuid.uuid4().hex
    )
    with Cluster(
        directory,
        request.config.getoption("--server-build"),
        request.config.getoption("--configuration"),
    ) as services:
        yield services
