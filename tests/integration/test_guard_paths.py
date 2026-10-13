import pytest
import requests
import socket
import uuid
from harness.checks import require

pytestmark = pytest.mark.integration


@pytest.mark.feature("PROTO-HANDLER-GUARDS")
@pytest.mark.parametrize(
    "message_id", [1007, 1009, 1013, 1017, 1025, 1027, 1029, 1031, 1033, 1035, 1038]
)
def test_authenticated_empty_requests_are_rejected(cluster, message_id):
    account = cluster.register()
    peer = cluster.login(account)
    require(
        peer.request(message_id, {}).get("error") != 0, "Incomplete authenticated request accepted"
    )
    require(peer.request(1023, {}).get("error") == 0, "Invalid input poisoned a valid session")


@pytest.mark.feature("AUTH-HTTP-GUARDS")
@pytest.mark.parametrize(
    "route", ["/get_verifycode", "/user_register", "/user_login", "/reset_pwd"]
)
def test_invalid_json_and_missing_fields(cluster, route):
    for content in ("{", "{}"):
        response = requests.post(
            cluster.gate_url + route,
            data=content,
            headers={"Content-Type": "application/json"},
            timeout=10,
        )
        require(
            response.status_code == 200 and response.json().get("error") != 0,
            "Malformed HTTP request accepted",
        )


@pytest.mark.feature("AUTH-LENGTH")
@pytest.mark.parametrize(
    "route,field,length",
    [
        ("/user_register", "user", 65),
        ("/user_register", "email", 256),
        ("/user_register", "passwd", 129),
        ("/reset_pwd", "user", 65),
        ("/reset_pwd", "email", 256),
        ("/reset_pwd", "passwd", 129),
        ("/user_login", "email", 256),
        ("/user_login", "passwd", 129),
    ],
)
def test_http_storage_length_limits(cluster, route, field, length):
    content = {
        "user": "User",
        "email": "test@example.test",
        "passwd": "Password123",
        "confirm": "Password123",
        "verifycode": "123456",
    }
    content[field] = "a" * length
    require(
        cluster.post(route, content).get("error") == 1001, "Overlong HTTP field was not rejected"
    )


@pytest.mark.feature("AUTH-HASH-INTEGRITY")
@pytest.mark.parametrize(
    "column,value",
    [("password_salt", "z" * 32), ("password_hash", "z" * 64), ("password_iterations", 0)],
)
def test_corrupt_credential_metadata_never_authenticates(cluster, column, value):
    account = cluster.register()
    with cluster.sql(root=True) as db:
        with db.cursor() as cursor:
            require(column in {"password_salt", "password_hash", "password_iterations"})
            cursor.execute(f"UPDATE users SET {column}=%s WHERE id=%s", (value, account["uid"]))
    response = cluster.post("/user_login", {"email": account["email"], "passwd": account["passwd"]})
    require(response.get("error") == 1009, "Corrupt hash metadata accepted or misclassified")


@pytest.mark.feature("HTTP-BOUNDARY")
@pytest.mark.parametrize(
    "method,path,expected",
    [
        ("GET", "/unknown", 404),
        ("POST", "/unknown", 404),
        ("PUT", "/get_test", 405),
        ("GET", "/get_test?key=%ZZ", 400),
    ],
)
def test_unknown_http_routes_and_invalid_escaping(cluster, method, path, expected):
    # Preserve malformed escapes: HTTP libraries may silently requote them.
    with socket.create_connection(("127.0.0.1", cluster.ports["gate"]), timeout=10) as peer:
        peer.sendall(
            f"{method} {path} HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n".encode()
        )
        header = bytearray()
        while b"\r\n" not in header:
            chunk = peer.recv(1024)
            require(chunk, "HTTP response missing")
            header.extend(chunk)
    require(int(bytes(header).split(b" ")[1]) == expected, f"Unexpected status for {method} {path}")


@pytest.mark.feature("HTTP-BOUNDARY")
@pytest.mark.parametrize("length", [32767, 32768, 32769])
def test_http_body_boundary(cluster, length):
    response = requests.post(cluster.gate_url + "/unknown", data=b"x" * length, timeout=10)
    require(response.status_code == (413 if length > 32768 else 404))


@pytest.mark.feature("HTTP-BOUNDARY")
def test_oversized_header_and_bad_query_do_not_crash_gateway(cluster):
    with socket.create_connection(("127.0.0.1", cluster.ports["gate"]), timeout=5) as peer:
        peer.sendall(
            b"GET /get_test HTTP/1.1\r\nHost: localhost\r\nX-Oversize: " + b"x" * 8192 + b"\r\n\r\n"
        )
        try:
            require(peer.recv(1024) == b"", "Oversize header was accepted")
        except ConnectionResetError:
            pass
    for path in (
        "/get_test?trailing=%",
        "/get_test?lower=%aa",
        "/get_test?space=a+b",
        "/get_test?numeric=%20",
        "/get_test?upper=%AF",
    ):
        with socket.create_connection(("127.0.0.1", cluster.ports["gate"]), timeout=5) as peer:
            peer.sendall(f"GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n".encode())
            response = peer.recv(4096)
            require(
                b"400" in response.split(b"\r\n")[0]
                if path.endswith("=%")
                else b"200" in response.split(b"\r\n")[0]
            )
    require(requests.get(cluster.gate_url + "/get_test", timeout=5).ok)


@pytest.mark.feature("USER-PROFILE-BOUNDARY")
@pytest.mark.parametrize(
    "field,value",
    [
        ("name", ""),
        ("name", "a" * 65),
        ("nick", "a" * 65),
        ("desc", "a" * 256),
        ("avatar", "not-base64"),
        ("avatar", "eHh4eA=="),
    ],
)
def test_profile_and_avatar_invalid_inputs_preserve_user(cluster, field, value):
    account = cluster.register()
    peer = cluster.login(account)
    content = {"name": account["user"], "nick": "initial", "desc": "initial"}
    content[field] = value
    require(peer.request(1031, content).get("error") != 0, "Invalid profile data committed")
    with cluster.sql() as db:
        with db.cursor() as cursor:
            cursor.execute("SELECT username,nick FROM users WHERE id=%s", (account["uid"],))
            name, nick = cursor.fetchone()
            require(
                name == account["user"] and nick != "initial",
                "Invalid update changed stored profile",
            )


@pytest.mark.feature("USER-SEARCH-BOUNDARY")
@pytest.mark.parametrize(
    "query", ["0", "-1", "999999999999999999999999999", "MissingUser", "' OR 1=1 --"]
)
def test_unknown_search_does_not_expose_another_user(cluster, query):
    account = cluster.register()
    peer = cluster.login(account)
    require(
        peer.request(1007, {"uid": query}).get("error") != 0,
        "Unknown/unsafe lookup matched an existing user",
    )


@pytest.mark.feature("FRIEND-IDEMPOTENCE")
def test_duplicate_friend_requests_do_not_duplicate_relations(cluster, pair):
    first, second, a, b, thread = pair
    a.request(
        1009,
        {"uid": first["uid"], "touid": second["uid"], "applyname": "repeat", "bakname": "repeat"},
    )
    b.request(1013, {"fromuid": second["uid"], "touid": first["uid"], "back": "repeat"})
    with cluster.sql() as db:
        with db.cursor() as cursor:
            cursor.execute(
                "SELECT COUNT(*) FROM friend WHERE (self_id=%s AND friend_id=%s) OR (self_id=%s AND friend_id=%s)",
                (first["uid"], second["uid"], second["uid"], first["uid"]),
            )
            require(cursor.fetchone()[0] == 2, "Friend relation duplicated")
            cursor.execute(
                "SELECT COUNT(*) FROM friend_apply WHERE from_uid=%s AND to_uid=%s",
                (first["uid"], second["uid"]),
            )
            require(cursor.fetchone()[0] == 1, "Application duplicated")


@pytest.mark.feature("SESSION-EXPIRY")
def test_pending_token_expiry_and_node_lease_selection(cluster):
    account = cluster.register()
    reply = cluster.post("/user_login", {"email": account["email"], "passwd": account["passwd"]})
    require(reply.get("error") == 0)
    cluster.redis.delete("utoken_" + str(account["uid"]))
    from harness.protocol import Peer

    with Peer("127.0.0.1", int(reply["port"])) as peer:
        require(
            peer.request(1005, {"uid": account["uid"], "token": reply["token"]}).get("error") != 0,
            "Expired pending token accepted",
        )
    require(
        cluster.post("/user_login", {"email": account["email"], "passwd": "wrong"}).get("error")
        == 1009
    )


@pytest.mark.feature("FRIEND-NODE-DELIVERY")
@pytest.mark.parametrize("recipient_node", ["chat1", "chat2"])
def test_same_and_cross_node_friend_and_message_notifications(cluster, recipient_node):
    first, second = cluster.register(), cluster.register()
    a, b = cluster.login(first, "chat1"), cluster.login(second, recipient_node)
    thread = cluster.friends(first, second, a, b)
    require(a.request(1023, {"friend_snapshot_id": 7}).get("friend_snapshot_id") == 7)
    require(b.request(1023, {"friend_snapshot_id": 8}).get("friend_snapshot_id") == 8)
    require(any(mid == 1011 for mid, body in b.notifications), "Friend apply notification missing")
    require(
        any(mid == 1015 for mid, body in a.notifications), "Friend acceptance notification missing"
    )
    sent = a.request(
        1017,
        {
            "fromuid": first["uid"],
            "touid": second["uid"],
            "thread_id": thread,
            "text_array": [
                {"unique_id": str(uuid.uuid4()), "content": "node delivery", "msg_type": 0}
            ],
        },
    )
    require(sent.get("error") == 0)
    require(b.request(1023, {}).get("error") == 0)
    require(
        any(
            mid == 1019
            and any(row["content"] == "node delivery" for row in body.get("chat_datas", []))
            for mid, body in b.notifications
        ),
        "Message notification missing",
    )
    a.close()
    b.close()


@pytest.mark.feature("MSG-IMAGE-INTEGRITY")
@pytest.mark.parametrize(
    "kind", ["empty", "unsupported", "bad-id", "not-ready", "wrong-owner", "bad-size"]
)
def test_message_content_and_resource_metadata_rejection(cluster, pair, kind):
    first, second, a, b, thread = pair
    file_id = uuid.uuid4().hex
    messages = [{"unique_id": str(uuid.uuid4()), "content": "hello", "msg_type": 0}]
    if kind == "empty":
        messages = []
    elif kind == "unsupported":
        messages[0]["msg_type"] = 2
    else:
        messages[0].update(content=file_id, msg_type=1)
        if kind == "bad-id":
            messages[0]["content"] = "../invalid"
        if kind == "wrong-owner":
            cluster.redis.set("resource_ready_" + file_id, "999999:999998:1")
        if kind == "bad-size":
            cluster.redis.set("resource_ready_" + file_id, f"{first['uid']}:{second['uid']}:broken")
    with cluster.sql() as db:
        with db.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM chat_message WHERE thread_id=%s", (thread,))
            before = cursor.fetchone()[0]
    response = a.request(
        1017,
        {
            "fromuid": first["uid"],
            "touid": second["uid"],
            "thread_id": thread,
            "text_array": messages,
        },
    )
    require(response.get("error") != 0, "Invalid text/image message was stored")
    with cluster.sql() as db:
        with db.cursor() as cursor:
            cursor.execute("SELECT COUNT(*) FROM chat_message WHERE thread_id=%s", (thread,))
            require(cursor.fetchone()[0] == before, "Rejected message changed history")


@pytest.mark.feature("CHAT-THREADS")
def test_thread_listing_and_readded_friend_reuses_history(cluster, pair):
    first, second, a, b, thread = pair
    reply = a.request(1025, {"uid": first["uid"], "thread_id": 0})
    require(
        reply.get("error") == 0
        and any(row["thread_id"] == thread for row in reply.get("threads", []))
    )
    require(a.request(1035, {"friend_uid": second["uid"]}).get("error") == 0)
    require(a.request(1027, {"uid": first["uid"], "other_id": second["uid"]}).get("error") != 0)
    require(
        cluster.friends(first, second, a, b) == thread,
        "Re-adding changed private conversation identity",
    )
