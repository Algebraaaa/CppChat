import base64
import hashlib
import uuid

import pytest

from harness.checks import eventually, require

pytestmark = pytest.mark.integration


def send(peer, first, second, thread, content, unique_id=None, msg_type=0):
    return peer.request(
        1017,
        {
            "fromuid": first["uid"],
            "touid": second["uid"],
            "thread_id": thread,
            "text_array": [
                {
                    "content": content,
                    "unique_id": unique_id or str(uuid.uuid4()),
                    "msg_type": msg_type,
                }
            ],
        },
    )


def history(peer, thread):
    messages, cursor = [], 0
    for _ in range(1000):
        reply = peer.request(1029, {"thread_id": thread, "message_id": cursor})
        require(reply.get("error") == 0, "History rejected")
        messages.extend(reply.get("chat_datas", []))
        if not reply.get("load_more"):
            return messages
        require(reply["last_message_id"] > cursor, "History cursor failed to advance")
        cursor = reply["last_message_id"]
    raise AssertionError("History pagination did not terminate")


@pytest.mark.smoke
@pytest.mark.feature("E2E-001")
def test_critical_journey(cluster, pair):
    first, second, a, b, thread = pair
    # Real SMTP registration, HTTP auth and opposite real chat nodes are in pair.
    reply = send(a, first, second, thread, "中文 English 😃")
    require(reply.get("error") == 0, "Text acknowledgement failed")
    notified = b.request(1023, {})
    require(notified.get("error") == 0, "Heartbeat failed")
    require(
        any(
            mid == 1019
            and any(item["content"] == "中文 English 😃" for item in body.get("chat_datas", []))
            for mid, body in b.notifications
        ),
        "Cross-node notification missing",
    )
    data = bytes(range(256)) * 257
    file_id = cluster.upload(first, second, data)
    require(
        send(a, first, second, thread, file_id, msg_type=1).get("error") == 0, "Image chat rejected"
    )
    resource = cluster.resource(second)
    output, offset = bytearray(), 0
    while offset < len(data):
        reply = resource.request(
            1054,
            {"uid": second["uid"], "token": second["token"], "file_id": file_id, "offset": offset},
        )
        require(reply.get("ok"), "Recipient download rejected")
        chunk = base64.b64decode(reply["data"], validate=True)
        require(chunk, "Download made no progress")
        output.extend(chunk)
        offset += len(chunk)
    require(
        hashlib.sha256(output).digest() == hashlib.sha256(data).digest(), "Image checksum mismatch"
    )
    b.close()
    require(
        send(a, first, second, thread, "offline message").get("error") == 0, "Offline send rejected"
    )
    b = cluster.login(second, "chat2")
    require(
        any(item["content"] == "offline message" for item in history(b, thread)),
        "Offline message missing",
    )
    # Client persistence/restart is exercised by Qt tests and desktop E2E checklist.
    code = cluster.code(first["email"])
    new_password = uuid.uuid4().hex
    cluster.credentials.append(new_password)
    request = {
        "user": first["user"],
        "email": first["email"],
        "passwd": new_password,
        "verifycode": code,
    }
    mismatch = dict(request, user="SomeoneElse")
    require(
        cluster.post("/reset_pwd", mismatch).get("error") == 1007, "Mismatch error misclassified"
    )
    require(cluster.post("/reset_pwd", request).get("error") == 0, "Password reset failed")
    require(cluster.post("/reset_pwd", request).get("error") != 0, "Verification code reused")
    require(
        cluster.post("/user_login", {"email": first["email"], "passwd": first["passwd"]}).get(
            "error"
        )
        != 0,
        "Old password accepted",
    )
    first["passwd"] = new_password
    a.close()
    a = cluster.login(first, "chat1")
    require(
        a.request(1035, {"friend_uid": second["uid"]}).get("error") == 0, "Delete friend failed"
    )
    require(
        send(a, first, second, thread, "forbidden").get("error") != 0,
        "Deleted friend may still send",
    )
    require(
        a.request(1029, {"thread_id": thread, "message_id": 0}).get("error") != 0,
        "Deleted friend may still fetch",
    )


@pytest.mark.feature("MSG-DEDUP")
def test_uuid_dedup_conflict_and_pagination(cluster, pair):
    first, second, a, b, thread = pair
    key = str(uuid.uuid4())
    initial = send(a, first, second, thread, "same", key)
    repeated = send(a, first, second, thread, "same", key)
    require(initial.get("error") == repeated.get("error") == 0, "Idempotent retry failed")
    require(
        initial["chat_datas"][0]["message_id"] == repeated["chat_datas"][0]["message_id"],
        "Retry assigned new identity",
    )
    require(
        send(a, first, second, thread, "different", key).get("error") != 0, "UUID conflict accepted"
    )
    for index in range(22):
        require(send(a, first, second, thread, f"page {index}").get("error") == 0)
    loaded = history(b, thread)
    require(sum(item.get("unique_id") == key for item in loaded) == 1, "Retry duplicated history")
    require(len({item["message_id"] for item in loaded}) == len(loaded), "Pages overlap")
    with cluster.sql() as db:
        with db.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM chat_message WHERE unique_id=%s", (key,))
            require(cursor.fetchone()[0] == 1, "Retry duplicated database row")


@pytest.mark.feature("USER-PROFILE")
@pytest.mark.feature("CONTACT-REMARK")
def test_search_profile_avatar_tags_and_owner_isolation(cluster, pair):
    first, second, a, b, thread = pair
    with cluster.sql() as db:
        with db.cursor() as cursor:
            cursor.execute("SHOW GRANTS")
            grants = " ".join(row[0] for row in cursor.fetchall())
            require(
                "CREATE" not in grants and "ALL PRIVILEGES" not in grants,
                "Avatar regression must use a business account without schema privileges",
            )
    require(a.request(1007, {"uid": str(second["uid"])}).get("uid") == second["uid"])
    require(a.request(1007, {"uid": second["user"]}).get("uid") == second["uid"])
    avatar = b"\xff\xd8test\xff\xd9"
    response = a.request(
        1031,
        {
            "name": first["user"],
            "nick": "昵称",
            "desc": "中文简介",
            "avatar": base64.b64encode(avatar).decode(),
        },
    )
    require(
        response.get("error") == 0 and response.get("committed"), "Profile update not committed"
    )
    require(base64.b64decode(b.request(1033, {"uid": first["uid"]})["avatar"]) == avatar)
    tags = a.request(1038, {"friend_uid": second["uid"], "tags": [" work ", "work", "中文"]})
    require(
        tags.get("error") == 0 and tags.get("tags") == ["work", "中文"], "Tags normalization failed"
    )
    remark = a.request(1038, {"friend_uid": second["uid"], "tags": ["work"], "back": "李同学"})
    require(remark.get("error") == 0 and remark.get("back") == "李同学", "Remark not acknowledged")
    # A legacy tags-only client must preserve the remark saved by a newer client.
    require(a.request(1038, {"friend_uid": second["uid"], "tags": ["work"]}).get("error") == 0)
    require(
        a.request(1038, {"friend_uid": second["uid"], "tags": [], "back": "x" * 65}).get("error")
        != 0,
        "Oversized remark accepted",
    )
    with cluster.sql() as db:
        with db.cursor() as cursor:
            cursor.execute(
                "SELECT back FROM friend WHERE self_id=%s AND friend_id=%s",
                (first["uid"], second["uid"]),
            )
            require(cursor.fetchone()[0] == "李同学", "Remark lost after legacy tags-only update")
            cursor.execute(
                "SELECT back FROM friend WHERE self_id=%s AND friend_id=%s",
                (second["uid"], first["uid"]),
            )
            require(cursor.fetchone()[0] != "李同学", "Remark crossed account boundary")
            cursor.execute(
                "SELECT tags FROM friend WHERE self_id=%s AND friend_id=%s",
                (second["uid"], first["uid"]),
            )
            row = cursor.fetchone()
            require(
                row is not None and "work" not in (row[0] or ""),
                "Friend tags crossed account boundary",
            )
    require(
        a.request(1038, {"friend_uid": second["uid"], "tags": [], "back": ""}).get("error") == 0,
        "Cannot clear remark",
    )


@pytest.mark.feature("SESSION-KICK")
def test_relogin_old_session_cannot_revoke_new_token(cluster):
    account = cluster.register()
    old = cluster.login(account, "chat1")
    new = cluster.login(account, "chat2")
    old.close()
    require(new.request(1023, {}).get("error") == 0)
    eventually(
        lambda: cluster.redis.get("resource_token_" + str(account["uid"])) == account["token"],
        description="new active credential retained",
    )
    with cluster.resource(account) as resource:
        reply = resource.request(
            1050,
            {
                "uid": account["uid"],
                "token": account["token"],
                "file_id": uuid.uuid4().hex,
                "recipient": account["uid"] + 100,
                "size": 1,
                "sha256": hashlib.sha256(b"x").hexdigest(),
            },
        )
        require(reply.get("error") != "invalid token", "Old cleanup revoked current token")
