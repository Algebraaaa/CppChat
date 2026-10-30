"""Logging silence must not stop real chat-node health renewal."""

import json
import time

import pytest

from harness.checks import eventually
from harness.cluster import Cluster


@pytest.fixture
def cluster(request, docker_available):
    # A fresh cluster guarantees zero business activity regardless of suite order.
    directory = request.config.getoption("--artifacts")
    from pathlib import Path

    with Cluster(
        Path(directory).resolve() / "quiet-logging-cluster",
        request.config.getoption("--server-build"),
        request.config.getoption("--configuration"),
    ) as services:
        yield services


@pytest.mark.smoke
@pytest.mark.feature("SERVER-LOGGING")
def test_quiet_heartbeat_still_renews_real_node_leases(cluster):
    keys = ["chat_node_ChatServer", "chat_node_ChatServer2"]
    owners = {key: json.loads(cluster.redis.get(key))["instance_id"] for key in keys}
    # Only the fixture Redis is touched. A renewed 30-second lease proves that
    # the ten-second maintenance timer kept running without its INFO message.
    for key in keys:
        assert cluster.redis.expire(key, 15)

    def renewed():
        return all(cluster.redis.ttl(key) > 20 for key in keys)

    eventually(renewed, 15, "quiet chat node maintenance")
    for key in keys:
        assert json.loads(cluster.redis.get(key))["instance_id"] == owners[key]
    time.sleep(1.1)  # allow the logger's normal periodic flush
    for service in ["ChatServer", "ChatServer2"]:
        files = list((cluster.directory / service / "logs").rglob("events*.log"))
        assert files, "Expected bounded file logging in the isolated fixture"
        text = "\n".join(path.read_text(encoding="utf-8") for path in files)
        assert "Heartbeat scan:" not in text
        assert "Business metrics:" not in text
        assert "TCP acceptor started" in text
        assert cluster.processes[service].poll() is None
