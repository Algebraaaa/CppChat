"""Real stored audio transport; synthetic PCM does not exercise physical devices."""

import base64
import hashlib
import io
import uuid
import wave

import pytest

from harness.checks import require
from harness.protocol import CHUNK_SIZE
from test_group_images import metadata, read
from test_groups import change, create
from test_journey import history, send

pytestmark = [pytest.mark.integration, pytest.mark.feature("AUDIO-MESSAGES")]


def pcm_wave(seconds=1):
    output = io.BytesIO()
    with wave.open(output, "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(16000)
        audio.writeframes(b"\x01\x00" * (16000 * seconds))
    return output.getvalue()


def audio_upload(cluster, account, data, recipient=0, thread=0):
    fields = dict(metadata(account, thread, data), media_type=3, recipient=recipient)
    peer = cluster.resource(account)
    require(peer.request(1050, fields).get("ok"), "Audio upload init failed")
    for offset in range(0, len(data), CHUNK_SIZE):
        reply = peer.request(
            1052,
            dict(
                fields,
                offset=offset,
                data=base64.b64encode(data[offset : offset + CHUNK_SIZE]).decode(),
            ),
        )
        require(reply.get("ok"), "Audio chunk failed")
    return fields


def test_private_audio_capability_legacy_fallback_dedupe_and_history(cluster):
    first, second = cluster.register(), cluster.register()
    a = cluster.login(first, "chat1", voice_messages=True)
    b = cluster.login(second, "chat2")  # Released old clients omit this capability.
    thread = cluster.friends(first, second, a, b)
    payload = pcm_wave()
    fields = audio_upload(cluster, first, payload, recipient=second["uid"])
    require(
        a.request(
            1005, {"uid": first["uid"], "token": first["token"], "voice_messages": False}
        ).get("error")
        != 0,
        "A repeated login unexpectedly changed an authenticated session",
    )
    require(not cluster.resource(first).request(1050, dict(fields, media_type=1)).get("ok"))
    require(send(a, first, second, thread, fields["file_id"], msg_type=1).get("error") != 0)
    key = str(uuid.uuid4())
    sent = send(a, first, second, thread, fields["file_id"], key, msg_type=3)
    require(sent.get("error") == 0)
    require(sent["chat_datas"][0]["duration_ms"] == 1000)
    require(sent["chat_datas"][0]["media_size"] == len(payload))
    require(
        send(a, first, second, thread, fields["file_id"], key, msg_type=3)["chat_datas"][0][
            "message_id"
        ]
        == sent["chat_datas"][0]["message_id"]
    )
    b.request(1023, {})
    require(
        any(
            mid == 1019
            and any(
                m["msg_type"] == 0 and "升级" in m["content"] for m in body.get("chat_datas", [])
            )
            for mid, body in b.notifications
        )
    )
    legacy = [message for message in history(b, thread) if message.get("unique_id") == key]
    require(len(legacy) == 1 and legacy[0]["msg_type"] == 0 and "升级" in legacy[0]["content"])
    require(send(b, second, first, thread, fields["file_id"], msg_type=3).get("error") != 0)
    b.close()
    b = cluster.login(second, "chat2", voice_messages=True)
    upgraded = [message for message in history(b, thread) if message.get("unique_id") == key]
    require(
        len(upgraded) == 1 and upgraded[0]["msg_type"] == 3 and upgraded[0]["duration_ms"] == 1000
    )
    require(base64.b64decode(read(cluster, second, fields)["data"]) == payload)
    outsider = cluster.register()
    cluster.login(outsider)
    require(not read(cluster, outsider, fields).get("ok"))
    require(
        (cluster.directory / "ResourceServer/images/audio" / (fields["file_id"] + ".bin")).exists()
    )
    require(
        not (cluster.directory / "ResourceServer/images" / (fields["file_id"] + ".json")).exists()
    )


def test_group_audio_offline_scope_and_membership_revocation(cluster):
    first, second = cluster.register(), cluster.register()
    a = cluster.login(first, "chat1", voice_messages=True)
    b = cluster.login(second, "chat2", voice_messages=True)
    cluster.friends(first, second, a, b)
    thread = create(a, [second["uid"]])["group"]["thread_id"]
    other = create(a, [second["uid"]])["group"]["thread_id"]
    payload = pcm_wave(60)
    fields = audio_upload(cluster, first, payload, thread=thread)
    peer = cluster.resource(second)
    data = b""
    while len(data) < len(payload):
        reply = peer.request(
            1054, dict(fields, uid=second["uid"], token=second["token"], offset=len(data))
        )
        require(reply.get("ok"))
        chunk = base64.b64decode(reply["data"])
        require(chunk)
        data += chunk
    require(hashlib.sha256(data).hexdigest() == fields["sha256"])
    require(send(a, first, {"uid": 0}, other, fields["file_id"], msg_type=3).get("error") != 0)
    b.close()
    require(send(a, first, {"uid": 0}, thread, fields["file_id"], msg_type=3).get("error") == 0)
    b = cluster.login(second, "chat2", voice_messages=True)
    require(history(b, thread)[0]["duration_ms"] == 60000)
    require(change(a, "remove", thread, member_uid=second["uid"]).get("error") == 0)
    require(not read(cluster, second, fields).get("ok"))
    require(change(a, "add", thread, member_uid=second["uid"]).get("error") == 0)
    require(change(a, "transfer", thread, member_uid=second["uid"]).get("error") == 0)
    require(change(a, "leave", thread).get("error") == 0)
    require(not read(cluster, first, fields).get("ok"))
    require(change(b, "disband", thread).get("error") == 0)
    require(not read(cluster, second, fields).get("ok"))


@pytest.mark.parametrize("mutation", ["short", "long", "stereo", "rate", "float", "trailing"])
def test_audio_rejects_invalid_format_before_ready(cluster, pair, mutation):
    first, second, _, _, _ = pair
    payload = bytearray(pcm_wave())
    if mutation == "short":
        payload = payload[:44] + payload[44:-2]
    elif mutation == "long":
        payload = payload[:44] + bytearray(1920002)
    elif mutation == "stereo":
        payload[22] = 2
    elif mutation == "rate":
        payload[24] = 1
    elif mutation == "float":
        payload[20] = 3
    else:
        payload += b"x"
    fields = dict(metadata(first, 0, payload), recipient=second["uid"], media_type=3)
    peer = cluster.resource(first)
    init = peer.request(1050, fields)
    if init.get("ok"):
        last = None
        for offset in range(0, len(payload), CHUNK_SIZE):
            last = peer.request(
                1052,
                dict(
                    fields,
                    offset=offset,
                    data=base64.b64encode(payload[offset : offset + CHUNK_SIZE]).decode(),
                ),
            )
        require(last and not last.get("ok"))
    require(not cluster.redis.exists("resource_ready_" + fields["file_id"]))
