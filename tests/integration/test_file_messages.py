"""Stored file transport, type isolation, resume and capability fallback."""
import base64
import hashlib
import uuid

import pytest
from harness.checks import require
from harness.protocol import CHUNK_SIZE
from test_group_images import metadata, read
from test_groups import change, create
from test_journey import history, send

pytestmark = [pytest.mark.integration, pytest.mark.feature("FILE-MESSAGES")]


def upload(cluster, account, payload, recipient=0, thread=0):
    fields = dict(metadata(account, thread, payload), recipient=recipient, media_type=2)
    resource = cluster.resource(account)
    require(resource.request(1050, fields).get("ok"))
    first = resource.request(1052, dict(fields, offset=0,
        data=base64.b64encode(payload[:CHUNK_SIZE]).decode()))
    require(first.get("ok"))
    resource.close()
    resource = cluster.resource(account)
    resumed = resource.request(1050, fields)
    require(resumed.get("ok") and resumed["offset"] == min(len(payload), CHUNK_SIZE))
    for offset in range(resumed["offset"], len(payload), CHUNK_SIZE):
        result = resource.request(1052, dict(fields, offset=offset,
            data=base64.b64encode(payload[offset:offset + CHUNK_SIZE]).decode()))
        require(result.get("ok"))
    return fields


@pytest.mark.smoke
def test_private_file_resume_integrity_type_scope_dedupe_and_legacy(cluster):
    first, second, outsider = cluster.register(), cluster.register(), cluster.register()
    a = cluster.login(first, "chat1", file_messages=True)
    b = cluster.login(second, "chat2", file_messages=True, device="android")
    cluster.login(outsider, "chat2")
    thread = cluster.friends(first, second, a, b)
    payload = bytes(range(256)) * 500
    fields = upload(cluster, first, payload, recipient=second["uid"])
    key = str(uuid.uuid4())
    result = send(a, first, second, thread, fields["file_id"], key, msg_type=2)
    require(result.get("error") == 0)
    stored = result["chat_datas"][0]
    require(stored["msg_type"] == 2 and stored["media_size"] == len(payload))
    b.request(1023, {})
    require(any(mid == 1019 and any(m.get("unique_id") == key and m.get("msg_type") == 2
        for m in body.get("chat_datas", [])) for mid, body in b.notifications))
    require(send(a, first, second, thread, fields["file_id"], key, msg_type=2)["chat_datas"][0]["message_id"] == stored["message_id"])
    require(send(a, first, second, thread, fields["file_id"], msg_type=1).get("error") != 0)
    require(send(a, first, second, thread, fields["file_id"], msg_type=3).get("error") != 0)
    require(not cluster.resource(first).request(1050, dict(fields, media_type=1)).get("ok"))
    require(not read(cluster, outsider, fields).get("ok"))
    downloaded = bytearray()
    while len(downloaded) < len(payload):
        chunk = read(cluster, second, fields, len(downloaded))
        require(chunk.get("ok") and chunk["sha256"] == hashlib.sha256(payload).hexdigest())
        downloaded.extend(base64.b64decode(chunk["data"]))
    require(bytes(downloaded) == payload)
    require(any(row.get("unique_id") == key and row["msg_type"] == 2 for row in history(b, thread)))
    b.close()
    legacy = cluster.login(second, "chat2", device="android")
    rows = [row for row in history(legacy, thread) if row.get("unique_id") == key]
    require(len(rows) == 1 and rows[0]["msg_type"] == 0 and "升级" in rows[0]["content"])
    own_file = upload(cluster, second, payload, recipient=first["uid"])
    require(legacy.request(1005, dict(uid=second["uid"], token=second["token"],
                                    file_messages=True, device="android")).get("error") != 0)
    require(send(legacy, second, first, thread, own_file["file_id"], msg_type=2).get("error") != 0,
            "Rejected repeated login changed the established file capability")


def test_group_file_membership_cross_scope_and_live_revocation(cluster):
    first, second = cluster.register(), cluster.register()
    a = cluster.login(first, "chat1", file_messages=True)
    b = cluster.login(second, "chat2", file_messages=True)
    private = cluster.friends(first, second, a, b)
    thread = create(a, [second["uid"]])["group"]["thread_id"]
    other = create(a, [second["uid"]])["group"]["thread_id"]
    fields = upload(cluster, first, b"group file" * 8000, thread=thread)
    sent = send(a, first, {"uid": 0}, thread, fields["file_id"], msg_type=2)
    require(sent.get("error") == 0)
    require(send(a, first, {"uid": 0}, other, fields["file_id"], msg_type=2).get("error") != 0)
    require(send(a, first, second, private, fields["file_id"], msg_type=2).get("error") != 0)
    require(read(cluster, second, fields).get("ok"))
    require(change(b, "leave", thread).get("error") == 0)
    require(not read(cluster, second, fields).get("ok"))


def test_native_mobile_facade_file_roundtrip_over_verified_tls(cluster):
    import json
    import os
    from pathlib import Path
    import subprocess
    import time
    from contextlib import ExitStack
    from harness.cluster import qt_environment, redact
    from harness.tls import certificates, TLSProxy

    build = Path(os.environ.get("CPPCHAT_MOBILE_BUILD", ""))
    probe = build / ("mobile_file_probe.exe" if os.name == "nt" else "mobile_file_probe")
    if not probe.is_file():
        pytest.skip("BLOCKED: build mobile_file_probe and set CPPCHAT_MOBILE_BUILD")
    first, second = cluster.register(), cluster.register()
    a = cluster.login(first, "chat1", file_messages=True)
    b = cluster.login(second, "chat2", file_messages=True)
    thread = cluster.friends(first, second, a, b)
    login = cluster.post("/user_login", dict(email=first["email"], passwd=first["passwd"]))
    require(login.get("error") == 0)
    cluster.credentials.append(login["token"])
    directory = cluster.directory / "mobile-file-native"
    directory.mkdir()
    config = directory / "private.json"
    process = None
    ca, cert, key = certificates(directory / "tls")
    try:
        with ExitStack() as stack:
            chat = stack.enter_context(TLSProxy(cert, key, ("127.0.0.1", cluster.ports["chat1"])))
            resource = stack.enter_context(TLSProxy(cert, key, ("127.0.0.1", cluster.ports["resource"])))
            (directory / "config.ini").write_text(
                "[GateServer]\nurl=https://isolated.example.invalid\n[ChatServer]\ntls=true\n"
                f"[ResourceServer]\nhost=127.0.0.1\nport={resource.port}\ntls=true\n", encoding="utf-8")
            config.write_text(json.dumps(dict(directory=str(directory), uid=first["uid"],
                thread=thread, token=login["token"], port=chat.port, ca=str(ca))), encoding="utf-8")
            environment = qt_environment(build)
            environment["QT_QPA_PLATFORM"] = "offscreen"
            if os.environ.get("CPPCHAT_QT_BUILD"):
                environment["QT_PLUGIN_PATH"] = str(Path(os.environ["CPPCHAT_QT_BUILD"]))
            log = directory / "native.log"
            with log.open("wb") as output:
                process = subprocess.Popen([str(probe.resolve()), str(config)], env=environment,
                    stdout=output, stderr=subprocess.STDOUT)
                file_id = None
                deadline = time.monotonic() + 30
                while file_id is None and time.monotonic() < deadline and process.poll() is None:
                    b.request(1023, {})
                    for mid, body in b.notifications:
                        if mid == 1019:
                            for row in body.get("chat_datas", []):
                                if row.get("msg_type") == 2 and row.get("send_uid", first["uid"]) == first["uid"]:
                                    file_id = row["content"]
                    time.sleep(0.05)
                require(file_id is not None, "Native facade did not send a confirmed file")
                payload = bytearray()
                while True:
                    chunk = read(cluster, second, dict(file_id=file_id), len(payload))
                    require(chunk.get("ok"))
                    payload.extend(base64.b64decode(chunk["data"]))
                    if chunk["complete"]:
                        break
                # Re-upload the exact standard CBOR document under the other sender's scope.
                reflected = upload(cluster, second, bytes(payload), recipient=first["uid"])
                require(send(b, second, first, thread, reflected["file_id"], msg_type=2).get("error") == 0)
                require(process.wait(timeout=25) == 0, "Native file receive/save probe failed")
            require("FILE_ROUNDTRIP_SHA256_OK" in log.read_text(encoding="utf-8", errors="replace"))
            require((directory / "received.bin").read_bytes() == bytes(range(256)) * 1024)
    finally:
        if process and process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
        config.unlink(missing_ok=True)
        (directory / "config.ini").unlink(missing_ok=True)
        key.unlink(missing_ok=True)
        for path in directory.rglob("*.log"):
            path.write_text(redact(path.read_text(encoding="utf-8", errors="replace"), cluster.credentials), encoding="utf-8")
