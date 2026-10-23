"""Desktop and Android slots own independent leases; delivery fans out per node."""

from concurrent.futures import ThreadPoolExecutor
import hashlib
import uuid
import pytest
from harness.checks import eventually, require
from harness.protocol import Peer
from test_journey import send, history

pytestmark = [pytest.mark.integration, pytest.mark.feature("MULTI-DEVICE")]


def heartbeat(peer):
    return peer.request(1023, {}).get("error") == 0


def notified(peer, marker):
    require(heartbeat(peer), "Device heartbeat rejected")
    return any(
        mid == 1019 and any(m.get("content") == marker for m in body.get("chat_datas", []))
        for mid, body in peer.notifications
    )


@pytest.mark.parametrize("phone_node", ["chat1", "chat2"])
def test_live_fanout_self_sync_replacement_and_logout(cluster, pair, phone_node):
    first, second, desktop, remote, thread = pair
    desktop_token = first["token"]
    phone_account = dict(first)
    phone = cluster.login(phone_account, phone_node, device="android")
    require(heartbeat(desktop) and heartbeat(phone), "One device displaced the other")
    with Peer("127.0.0.1", cluster.ports[phone_node]) as replay:
        require(
            replay.request(
                1005, {"uid": first["uid"], "token": desktop_token, "device": "android"}
            ).get("error")
            != 0,
            "Desktop active token replaced Android slot",
        )
    require(heartbeat(phone), "Rejected cross-device replay retired the phone")
    marker = "multi-device-" + uuid.uuid4().hex
    require(send(remote, second, first, thread, marker).get("error") == 0)
    require(notified(desktop, marker) and notified(phone, marker), "Recipient fanout incomplete")
    own = "phone-sent-" + uuid.uuid4().hex
    require(send(phone, first, second, thread, own).get("error") == 0)
    require(notified(desktop, own), "Sender companion missed committed message")
    require(notified(remote, own), "Peer missed phone message")
    require(
        len([m for m in history(desktop, thread) if m["content"] == own]) == 1,
        "Self-sync duplicated durable message",
    )

    # Both independent resource credentials remain valid.
    for account in (dict(first, token=desktop_token), phone_account):
        with cluster.resource(account) as resource:
            body = {
                "uid": first["uid"],
                "token": account["token"],
                "file_id": uuid.uuid4().hex,
                "recipient": second["uid"],
                "size": 1,
                "sha256": hashlib.sha256(b"x").hexdigest(),
            }
            require(
                resource.request(1050, body).get("ok"), "Other device invalidated resource token"
            )

    replacement_account = dict(first)
    replacement = cluster.login(
        replacement_account, "chat2" if phone_node == "chat1" else "chat1", device="android"
    )
    require(
        heartbeat(desktop) and heartbeat(replacement), "Same-platform replacement affected desktop"
    )
    require(phone.receive()[0] == 1021, "Old Android session was not retired")
    phone.close()
    # Late cleanup of the old phone must not revoke the replacement.
    require(heartbeat(replacement), "Old cleanup revoked replacement")
    replacement.close()
    eventually(
        lambda: cluster.redis.get("resource_token_" + str(first["uid"]) + ":android") is None,
        description="Phone logout did not revoke phone token",
    )
    require(
        cluster.redis.get("resource_token_" + str(first["uid"])) == desktop_token,
        "Phone logout revoked desktop token",
    )
    require(heartbeat(desktop), "Desktop disconnected after phone logout")


def test_concurrent_http_allocations_and_device_validation(cluster):
    account = cluster.register()
    with ThreadPoolExecutor(max_workers=2) as pool:
        allocations = list(
            pool.map(
                lambda _: cluster.post(
                    "/user_login", {"email": account["email"], "passwd": account["passwd"]}
                ),
                range(2),
            )
        )
    for device, allocation, node in zip(("desktop", "android"), allocations, ("chat1", "chat2")):
        peer = Peer("127.0.0.1", cluster.ports[node])
        cluster.peers.append(peer)
        require(
            peer.request(
                1005, {"uid": account["uid"], "token": allocation["token"], "device": device}
            ).get("error")
            == 0,
            "Concurrent HTTP allocation invalidated another pending token",
        )
    uid = str(account["uid"])
    require(cluster.redis.get("usession_" + uid) is not None)
    require(cluster.redis.get("usession_" + uid + ":android") is not None)
    allocation = cluster.post(
        "/user_login", {"email": account["email"], "passwd": account["passwd"]}
    )
    with Peer("127.0.0.1", cluster.ports["chat1"]) as invalid:
        require(
            invalid.request(
                1005,
                {
                    "uid": account["uid"],
                    "token": allocation["token"],
                    "device": "arbitrary-third-slot",
                },
            ).get("error")
            != 0,
            "Unbounded device slots accepted",
        )
