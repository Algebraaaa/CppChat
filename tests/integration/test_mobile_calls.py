"""Calls bind device slots and session IDs even with four concurrent logins."""

import uuid
import pytest
from harness.checks import require
from harness.protocol import encode
from test_voice_calls import control, notification
from test_video_calls import frame as video_frame

pytestmark = [pytest.mark.integration, pytest.mark.smoke, pytest.mark.feature("MOBILE-CALLS")]


@pytest.mark.parametrize("video", [False, True])
@pytest.mark.parametrize("phone_node", ["chat1", "chat2"])
def test_four_slots_phone_priority_bound_media_and_no_sibling_takeover(
    cluster, pair, phone_node, video
):
    first, second, desktop_a, desktop_b, _ = pair
    phone_a = cluster.login(dict(first), phone_node, device="android")
    phone_b = cluster.login(
        dict(second), "chat2" if phone_node == "chat1" else "chat1", device="android"
    )
    call = uuid.uuid4().hex
    require(control(phone_a, second["uid"], call, "invite", video=video)["error"] == 0)
    require(notification(phone_b, call, "invite")["peer"] == first["uid"])
    require(
        control(desktop_b, first["uid"], call, "accept", video=video)["error"] != 0,
        "Sibling desktop stole the phone invitation",
    )
    require(control(phone_b, first["uid"], call, "accept", video=video)["error"] == 0)
    notification(phone_a, call, "accept")
    # A sibling's new login must not invalidate the Android call binding.
    desktop_a.close()
    desktop_a = cluster.login(dict(first), "chat2", device="desktop")
    phone_a.socket.sendall(
        encode(1072, dict(peer=second["uid"], call_id=call, seq=1, video=video, audio="+P/+"))
    )
    require(notification(phone_b, call, audio=True)["audio"] == "+P/+")
    phone_b.socket.sendall(
        encode(1072, dict(peer=first["uid"], call_id=call, seq=1, video=video, audio="+P/+"))
    )
    require(notification(phone_a, call, audio=True)["audio"] == "+P/+")
    if video:
        video_frame(phone_a, second["uid"], call)
        require(notification(phone_b, call, video=True)["seq"] == 1)
        video_frame(phone_b, first["uid"], call)
        require(notification(phone_a, call, video=True)["seq"] == 1)
    require(
        control(desktop_a, second["uid"], call, "end", video=video)["error"] != 0,
        "Sibling desktop ended the active phone call",
    )
    require(control(phone_b, first["uid"], call, "end", video=video)["error"] == 0)
    notification(phone_a, call, "end")
    for peer in (phone_a, phone_b, desktop_a, desktop_b):
        require(peer.request(1023, {}).get("error") == 0)
    require(not cluster.redis.exists("voice:" + str(first["uid"])))


@pytest.mark.parametrize("video", [False, True])
def test_desktop_caller_to_android_only_peer_and_replaced_phone_is_rejected(cluster, pair, video):
    first, second, desktop_a, desktop_b, _ = pair
    desktop_b.close()
    phone_b = cluster.login(dict(second), "chat2", device="android")
    call = uuid.uuid4().hex
    require(control(desktop_a, second["uid"], call, "invite", video=video)["error"] == 0)
    notification(phone_b, call, "invite")
    require(control(phone_b, first["uid"], call, "accept", video=video)["error"] == 0)
    notification(desktop_a, call, "accept")
    replacement = cluster.login(dict(second), "chat1", device="android")
    require(control(replacement, first["uid"], call, "end", video=video)["error"] != 0)
    desktop_a.socket.sendall(
        encode(1072, dict(peer=second["uid"], call_id=call, seq=1, video=video, audio="+P/+"))
    )
    require(desktop_a.receive()[1].get("error") != 0)
