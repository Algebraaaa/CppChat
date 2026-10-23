"""Real lease transitions and transient relay faults, with isolated users/services."""

import json
import time
import uuid
from pathlib import Path

import pytest

from harness.cluster import Cluster
from harness.protocol import encode
from harness.tls import TLSProxy
from test_voice_calls import control, frame, notification

pytestmark = [pytest.mark.integration, pytest.mark.feature("VOICE-CALL")]


def establish(cluster, node="chat2", video=False):
    first, second = cluster.register(), cluster.register()
    a = cluster.login(first, "chat1")
    b = cluster.login(second, node, device="android")
    cluster.friends(first, second, a, b)
    call = uuid.uuid4().hex
    options = {"video": video}
    assert control(a, second["uid"], call, "invite", **options)["error"] == 0
    notification(b, call, "invite")
    assert control(b, first["uid"], call, "accept", **options)["error"] == 0
    notification(a, call, "accept")
    return first, second, a, b, call, options


@pytest.mark.parametrize("node", ["chat1", "chat2"])
@pytest.mark.parametrize("video", [False, True])
def test_valid_media_refreshes_liveness_but_expired_call_never_revives(cluster, node, video):
    first, second, a, b, call, options = establish(cluster, node, video)
    try:
        keys = ["voice:" + str(account["uid"]) for account in (first, second)]
        lease = json.loads(cluster.redis.get(keys[0]))
        seconds, micros = cluster.redis.time()
        now = seconds * 1000 + micros / 1000
        # Model a gap beyond the old 15-second cutoff, without a slow wall-clock test.
        lease["ta"] = lease["tb"] = now - 18000
        for key in keys:
            cluster.redis.set(key, json.dumps(lease), px=45000)
        for sender, receiver, recipient in ((a, b, second), (b, a, first)):
            sender.socket.sendall(
                encode(
                    1072, dict(peer=recipient["uid"], call_id=call, seq=1, audio="+P/+", **options)
                )
            )
            assert notification(receiver, call, audio=True)["seq"] == 1
        refreshed = json.loads(cluster.redis.get(keys[0]))
        assert refreshed["id"] == call and refreshed["ta"] > now - 1000
        assert refreshed["tb"] > now - 1000
        pong = control(a, second["uid"], call, "ping", **options)
        assert pong["error"] == 0 and 0 <= pong["peer_idle_ms"] < 1500
        if video:
            assert control(a, second["uid"], call, "camera", enabled=False, **options)["error"] == 0
            notification(b, call, "camera")
            assert control(b, first["uid"], call, "ping", **options)["peer_camera_enabled"] is False
        lease = json.loads(cluster.redis.get(keys[0]))
        lease["ta"] = lease["tb"] = now - 31000
        for key in keys:
            cluster.redis.set(key, json.dumps(lease), px=45000)
        assert control(a, second["uid"], call, "ping", **options)["error"] == 1023
        assert not any(cluster.redis.exists(key) for key in keys)
        assert control(b, first["uid"], call, "accept", **options)["error"] == 1023
        assert not any(cluster.redis.exists(key) for key in keys)
    finally:
        a.close()
        b.close()


class RelayedCluster(Cluster):
    def write_config(self, name, config):
        if name == "ChatServer":
            self.relay = self.stack.enter_context(
                TLSProxy(None, None, ("127.0.0.1", self.ports["rpc2"]), idle_timeout=45)
            )
            config["Peer"]["Port"] = str(self.relay.port)
        super().write_config(name, config)


@pytest.fixture(scope="module")
def relay_cluster(request, docker_available):
    out = Path(request.config.getoption("--artifacts")).resolve() / ("relay-" + uuid.uuid4().hex)
    with RelayedCluster(
        out, request.config.getoption("--server-build"), request.config.getoption("--configuration")
    ) as services:
        yield services, services.relay


def test_one_failed_media_relay_does_not_delete_live_call(relay_cluster):
    cluster, proxy = relay_cluster
    first, second, a, b, call, options = establish(cluster)
    try:
        proxy.stall_for(0.7)  # Longer than the production 300 ms media RPC deadline.
        frame(a, second["uid"], call, 1)
        assert control(a, second["uid"], call, "ping")["error"] == 0
        assert json.loads(cluster.redis.get("voice:" + str(first["uid"])))["id"] == call
        time.sleep(0.8)
        frame(a, second["uid"], call, 2)
        received = notification(b, call, audio=True)["seq"]
        if received != 2:
            received = notification(b, call, audio=True)["seq"]
        assert received == 2
        assert control(a, second["uid"], call, "end")["error"] == 0
        notification(b, call, "end")
    finally:
        a.close()
        b.close()


def test_failed_camera_notification_is_reconciled_without_hangup(relay_cluster):
    cluster, proxy = relay_cluster
    first, second, a, b, call, options = establish(cluster, video=True)
    try:
        proxy.stall_for(0.7)
        reply = control(a, second["uid"], call, "camera", enabled=False, **options)
        assert reply["error"] == 1024
        assert cluster.redis.exists("voice:" + str(first["uid"]))
        pong = control(b, first["uid"], call, "ping", **options)
        assert pong["error"] == 0 and pong["peer_camera_enabled"] is False
        time.sleep(0.8)
        assert control(a, second["uid"], call, "end", **options)["error"] == 0
    finally:
        a.close()
        b.close()
