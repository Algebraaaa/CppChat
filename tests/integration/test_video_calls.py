"""Actual same/cross-node relay, authentication and camera consent; no devices."""

import base64
import time
import uuid
import pytest
from harness.protocol import encode
from test_voice_calls import control, notification

pytestmark = [pytest.mark.integration, pytest.mark.feature("VIDEO-CALL")]


def frame(client, peer, call, sequence=1, image=None):
    # The relay is codec-opaque; native Qt tests separately decode real JPEGs.
    image = image or base64.b64encode(b"\xff\xd8\xff" + b"x" * 100).decode()
    client.socket.sendall(
        encode(1074, dict(peer=peer, call_id=call, seq=sequence, video=True, image=image))
    )


@pytest.mark.parametrize("node", ["chat1", "chat2"])
def test_video_consent_bidirectional_camera_and_replay(cluster, pair, node):
    first, second, a, b, _ = pair
    if node == "chat1":
        b.close()
        b = cluster.login(second, node)
    call = uuid.uuid4().hex
    assert control(a, second["uid"], call, "invite", video=True)["error"] == 0
    assert notification(b, call, "invite")["video"] is True
    frame(a, second["uid"], call)
    assert a.receive()[1]["error"] == 1023
    # A voice-only client cannot answer a video invitation without consent.
    assert control(b, first["uid"], call, "accept")["error"] == 1023
    assert control(b, first["uid"], call, "accept", video=True)["error"] == 0
    notification(a, call, "accept")
    frame(a, second["uid"], call)
    assert notification(b, call, video=True)["seq"] == 1
    frame(b, first["uid"], call)
    assert notification(a, call, video=True)["seq"] == 1
    frame(a, second["uid"], call)
    assert a.receive()[1]["error"] == 1023
    frame(a, second["uid"], call, 2, "!!!!")
    assert a.receive()[1]["error"] == 1001
    frame(a, second["uid"], call, 2, "/9j/" + "A" * 8000)
    assert a.receive()[1]["error"] == 1001
    assert control(a, second["uid"], call, "camera", video=True, enabled=False)["error"] == 0
    assert notification(b, call, "camera")["enabled"] is False
    frame(a, second["uid"], call, 2)
    assert a.receive()[1]["error"] == 1023
    assert control(a, second["uid"], call, "camera", video=True, enabled=True)["error"] == 0
    notification(b, call, "camera")
    time.sleep(0.15)
    frame(a, second["uid"], call, 2)
    assert notification(b, call, video=True)["seq"] == 2
    # Video and audio have independent monotonic counters and rate limits.
    a.socket.sendall(
        encode(1072, dict(peer=second["uid"], call_id=call, seq=1, video=True, audio="+P/+"))
    )
    assert notification(b, call, audio=True)["seq"] == 1
    assert control(b, first["uid"], call, "end", video=True)["error"] == 0
    notification(a, call, "end")
    frame(a, second["uid"], call, 3)
    assert a.receive()[1]["error"] == 1023


def test_voice_call_rejects_video_and_stale_login(cluster, pair):
    first, second, a, b, _ = pair
    call = uuid.uuid4().hex
    assert control(a, second["uid"], call, "invite")["error"] == 0
    notification(b, call, "invite")
    assert control(b, first["uid"], call, "accept")["error"] == 0
    notification(a, call, "accept")
    frame(a, second["uid"], call)
    assert a.receive()[1]["error"] == 1023
    replacement = cluster.login(second, "chat2")
    frame(a, second["uid"], call, 2)
    assert a.receive()[1]["error"] == 1010
    replacement.close()
