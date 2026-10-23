"""Real chat nodes and isolated Redis/MySQL; synthetic Opus, no microphone access."""

import base64
from contextlib import ExitStack
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

import pytest

from harness.protocol import encode
from harness.tls import TLSProxy, certificates

pytestmark = [pytest.mark.integration, pytest.mark.feature("VOICE-CALL")]


def control(peer, target, call, action, **options):
    request_id = str(uuid.uuid4())
    peer.socket.sendall(
        encode(
            1070, dict(peer=target, call_id=call, action=action, request_id=request_id, **options)
        )
    )
    for _ in range(100):
        message_id, body = peer.receive()
        if message_id == 1071 and body.get("request_id") == request_id:
            return body
        peer.notifications.append((message_id, body))
    raise AssertionError("Voice control reply missing")


def notification(peer, call, action=None, audio=False, video=False):
    expected_id = 1075 if video else 1073 if audio else 1071

    def matches(item):
        message_id, body = item
        return (
            message_id == expected_id
            and body.get("call_id") == call
            and (audio or video or body.get("action") == action)
            and body.get("incoming") is True
        )

    for index, item in enumerate(peer.notifications):
        if matches(item):
            return peer.notifications.pop(index)[1]
    for _ in range(100):
        item = peer.receive()
        if matches(item):
            return item[1]
        peer.notifications.append(item)
    raise AssertionError("Voice notification missing")


def frame(peer, target, call, sequence=1, audio="+P/+"):
    peer.socket.sendall(encode(1072, dict(peer=target, call_id=call, seq=sequence, audio=audio)))


@pytest.mark.smoke
@pytest.mark.parametrize("node", ["chat1", "chat2"])
def test_accept_bidirectional_audio_and_end(cluster, pair, node):
    first, second, a, b, _ = pair
    if node == "chat1":
        b.close()
        b = cluster.login(second, node)
    call = uuid.uuid4().hex
    assert control(a, second["uid"], call, "invite")["error"] == 0
    assert 30 <= cluster.redis.ttl("voice:" + str(first["uid"])) <= 40
    assert notification(b, call, "invite")["peer"] == first["uid"]
    # No media before consent and only the callee can accept.
    frame(a, second["uid"], call)
    assert a.receive()[1]["error"] == 1023
    assert control(a, second["uid"], call, "accept")["error"] == 1023
    assert control(b, first["uid"], call, "accept")["error"] == 0
    assert 40 <= cluster.redis.ttl("voice:" + str(first["uid"])) <= 45
    notification(a, call, "accept")
    assert control(b, first["uid"], call, "accept")["error"] == 1023
    # A valid 20ms Opus silence packet, conveyed unchanged in both directions.
    opus = base64.b64encode(bytes([0xF8, 0xFF, 0xFE])).decode()
    frame(a, second["uid"], call, 1, opus)
    assert notification(b, call, audio=True)["audio"] == opus
    frame(b, first["uid"], call, 1, opus)
    assert notification(a, call, audio=True)["audio"] == opus
    # Sequence replay and a guessed call ID must fail closed.
    frame(a, second["uid"], call, 1, opus)
    assert a.receive()[1]["error"] == 1023
    frame(a, second["uid"], uuid.uuid4().hex, 2, opus)
    assert a.receive()[1]["error"] == 1023
    assert control(a, second["uid"], call, "ping")["error"] == 0
    assert control(b, first["uid"], call, "end")["error"] == 0
    notification(a, call, "end")
    assert not cluster.redis.exists("voice:" + str(first["uid"]))
    assert not cluster.redis.exists("voice:" + str(second["uid"]))
    frame(a, second["uid"], call, 2, opus)
    assert a.receive()[1]["error"] == 1023


def test_reject_cancel_busy_and_nonfriend(cluster, pair):
    first, second, a, b, _ = pair
    outsider = cluster.register()
    c = cluster.login(outsider, "chat2")
    call = uuid.uuid4().hex
    assert control(c, first["uid"], call, "invite")["error"] == 1017
    cluster.friends(outsider, first, c, a)
    assert control(a, second["uid"], call, "invite")["error"] == 0
    notification(b, call, "invite")
    assert control(c, first["uid"], uuid.uuid4().hex, "invite")["error"] == 1021
    # No other user can terminate this call.
    assert control(c, first["uid"], call, "end")["error"] == 1023
    assert control(b, first["uid"], call, "cancel")["error"] == 1023
    assert control(b, first["uid"], call, "reject")["error"] == 0
    notification(a, call, "reject")
    assert not cluster.redis.exists("voice:" + str(first["uid"]))
    assert control(a, second["uid"], uuid.uuid4().hex, "invite")["error"] == 1025
    other = uuid.uuid4().hex
    assert control(b, first["uid"], other, "invite")["error"] == 0
    notification(a, other, "invite")
    assert control(b, first["uid"], other, "cancel")["error"] == 0
    notification(a, other, "cancel")


def test_expired_or_replaced_session_cannot_send(cluster, pair):
    first, second, a, b, _ = pair
    call = uuid.uuid4().hex
    assert control(a, second["uid"], call, "invite")["error"] == 0
    notification(b, call, "invite")
    assert control(b, first["uid"], call, "accept")["error"] == 0
    notification(a, call, "accept")
    replacement = cluster.login(second, "chat2")
    frame(a, second["uid"], call)
    assert a.receive()[1]["error"] == 1010
    assert control(replacement, first["uid"], call, "end")["error"] == 1010
    # Only fixture keys are expired; production Redis is never touched.
    cluster.redis.delete("voice:" + str(first["uid"]), "voice:" + str(second["uid"]))
    assert control(a, second["uid"], call, "ping")["error"] == 1023


def test_unauthenticated_and_malformed_requests(cluster, pair):
    from harness.protocol import Peer

    first, second, a, _, _ = pair
    with Peer("127.0.0.1", cluster.ports["chat1"]) as anonymous:
        assert control(anonymous, first["uid"], uuid.uuid4().hex, "invite")["error"] == 1010
    assert control(a, second["uid"], "bad", "invite")["error"] == 1001
    call = uuid.uuid4().hex
    assert control(a, first["uid"], call, "invite")["error"] == 1001
    assert control(a, second["uid"], call, "invalid")["error"] == 1001


@pytest.mark.smoke
@pytest.mark.parametrize(
    "tls,video,stall",
    [
        pytest.param(False, False, False, id="tcp-audio"),
        pytest.param(True, False, False, id="tls-audio"),
        pytest.param(False, True, False, id="tcp-video"),
        pytest.param(True, True, False, id="tls-video"),
        pytest.param(True, False, True, id="tls-jitter-audio"),
        pytest.param(True, True, True, id="tls-jitter-video"),
    ],
)
def test_two_native_qt_clients_encode_and_decode(cluster, pair, tls, video, stall):
    """Both clients use production TcpMgr/state/Opus code, with a synthetic source."""
    root = Path(__file__).resolve().parents[2]
    qt_build = Path(os.environ.get("CPPCHAT_QT_BUILD", root / "CppChat/build/debug"))
    suffix = ".exe" if os.name == "nt" else ""
    probe = qt_build / "tests/qt" / ("voice_transport_probe" + suffix)
    if not probe.is_file():
        pytest.skip("BLOCKED: build voice_transport_probe in the selected Qt build")
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    for setting in (qt_build / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if setting.startswith("Qt6_DIR:PATH="):
            qt_bin = Path(setting.split("=", 1)[1]).parents[2] / "bin"
            environment["PATH"] = str(qt_bin) + os.pathsep + environment.get("PATH", "")
    first, second, a, b, _ = pair
    a.close()
    b.close()
    directory = cluster.directory / ("qt-voice-" + uuid.uuid4().hex)
    directory.mkdir()
    processes, streams, configs = [], [], []
    proxies = ExitStack()
    ports = {node: cluster.ports[node] for node in ("chat1", "chat2")}
    ca = key = None
    try:
        if tls:
            ca, cert, key = certificates(directory / "tls")
            for node in ports:
                ports[node] = proxies.enter_context(
                    TLSProxy(
                        cert,
                        key,
                        ("127.0.0.1", cluster.ports[node]),
                        stall_after=2 if stall else None,
                        stall_seconds=14 if stall else 0,
                        idle_timeout=40 if stall else 5,
                    )
                ).port
        for index, account, other, node in (
            (0, second, first, "chat2"),
            (1, first, second, "chat1"),
        ):
            login = cluster.post(
                "/user_login", dict(email=account["email"], passwd=account["passwd"])
            )
            assert login.get("error") == 0
            cluster.credentials.append(login["token"])
            config = directory / f"private-{index}.json"
            config.write_text(
                json.dumps(
                    dict(
                        uid=account["uid"],
                        peer=other["uid"],
                        token=login["token"],
                        port=ports[node],
                        tls=tls,
                        ca_file=str(ca) if ca else "",
                        caller=bool(index),
                        video=video,
                        device="android" if stall and index == 0 else "desktop",
                        minimum_active_ms=20000 if stall else 0,
                        expected_gap_ms=10000 if stall else 0,
                        expected_recoveries=1 if stall else 0,
                        inject_replay=stall,
                        timeout_ms=35000 if stall else 17000,
                    )
                ),
                encoding="utf-8",
            )
            configs.append(config)
            log = directory / f"client-{index}.log"
            output = log.open("wb")
            streams.append(output)
            process = subprocess.Popen(
                [str(probe.resolve()), str(config.resolve())],
                env=environment,
                stdout=output,
                stderr=subprocess.STDOUT,
            )
            processes.append(process)
            timeout = time.monotonic() + 8
            while "READY" not in log.read_text(encoding="utf-8", errors="replace"):
                assert process.poll() is None, log.read_text(encoding="utf-8", errors="replace")
                assert time.monotonic() < timeout, "Native client login timed out"
                time.sleep(0.05)
        for index, process in enumerate(processes):
            assert process.wait(timeout=40 if stall else 20) == 0, (
                directory / f"client-{index}.log"
            ).read_text(encoding="utf-8", errors="replace")
            results = [
                json.loads(line)
                for line in (directory / f"client-{index}.log")
                .read_text(encoding="utf-8", errors="replace")
                .splitlines()
                if line.startswith("{")
            ]
            results = [result for result in results if "decoded" in result]
            assert results and results[-1]["decoded"] >= 40
            if video:
                assert results[-1]["video_decoded"] >= 5
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)
        for output in streams:
            output.close()
        for config in configs:
            config.unlink(missing_ok=True)
        proxies.close()
        if key:
            key.unlink(missing_ok=True)
