"""Real group image transport and live membership revocation; no production data."""

import base64
import hashlib
import ipaddress
import uuid

import psutil
import pytest

from harness.checks import require
from harness.protocol import CHUNK_SIZE
from test_groups import change, create
from test_journey import history, send

pytestmark = [pytest.mark.integration, pytest.mark.feature("GROUP-IMAGES")]


def metadata(account, thread, data):
    return {
        "uid": account["uid"],
        "token": account["token"],
        "file_id": uuid.uuid4().hex,
        "recipient": 0,
        "thread_id": thread,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
    }


def upload(cluster, account, thread, data):
    fields = metadata(account, thread, data)
    peer = cluster.resource(account)
    require(peer.request(1050, fields).get("ok"), "Group upload init denied")
    for offset in range(0, len(data), CHUNK_SIZE):
        require(
            peer.request(
                1052,
                dict(
                    fields,
                    offset=offset,
                    data=base64.b64encode(data[offset : offset + CHUNK_SIZE]).decode(),
                ),
            ).get("ok")
        )
    return fields


def read(cluster, account, fields, offset=0):
    return cluster.resource(account).request(
        1054, dict(fields, uid=account["uid"], token=account["token"], offset=offset)
    )


def test_group_images_resume_scope_cross_node_history_and_revocation(cluster, pair):
    first, second, a, b, private_thread = pair
    outsider = cluster.register()
    cluster.login(outsider, "chat2")
    thread = create(a, [second["uid"]])["group"]["thread_id"]
    other_thread = create(a, [second["uid"]])["group"]["thread_id"]
    for node, key in (("ChatServer", "media1"), ("ChatServer2", "media2")):
        listeners = [
            c.laddr.ip
            for c in psutil.Process(cluster.processes[node].pid).net_connections()
            if c.status == psutil.CONN_LISTEN and c.laddr.port == cluster.ports[key]
        ]
        # gRPC uses IPv4-mapped IPv6 sockets on Linux, while Windows reports IPv4.
        addresses = [ipaddress.ip_address(value) for value in listeners]
        require(
            bool(addresses)
            and all(
                (address.ipv4_mapped if isinstance(address, ipaddress.IPv6Address) else address)
                == ipaddress.IPv4Address("127.0.0.1")
                for address in addresses
            ),
            f"Media authorization escaped loopback: {listeners}",
        )
    data = b"group-image-fixture" * 2000
    fields = metadata(first, thread, data)
    peer = cluster.resource(first)
    require(peer.request(1050, fields).get("ok"))
    require(
        peer.request(
            1052, dict(fields, offset=0, data=base64.b64encode(data[:CHUNK_SIZE]).decode())
        ).get("ok")
    )
    peer.close()
    peer = cluster.resource(first)
    resumed = peer.request(1050, fields)
    require(resumed.get("ok") and resumed["offset"] == CHUNK_SIZE, "Group resume lost bytes")
    require(
        peer.request(
            1052, dict(fields, offset=CHUNK_SIZE, data=base64.b64encode(data[CHUNK_SIZE:]).decode())
        ).get("ok")
    )
    require(not read(cluster, outsider, fields).get("ok"), "Nonmember downloaded group image")
    directory = cluster.directory / "ResourceServer/images"
    require((directory / "groups" / (fields["file_id"] + ".bin")).is_file())
    require(
        not (directory / (fields["file_id"] + ".json")).exists(),
        "Old resource binary could bypass group policy after rollback",
    )
    denied = metadata(outsider, thread, b"x")
    require(not cluster.resource(outsider).request(1050, denied).get("ok"))
    reply = read(cluster, second, fields)
    require(reply.get("ok"))
    tail = read(cluster, second, fields, reply["offset"])
    downloaded = base64.b64decode(reply["data"]) + base64.b64decode(tail["data"])
    require(downloaded == data and hashlib.sha256(downloaded).hexdigest() == fields["sha256"])
    key = str(uuid.uuid4())
    sent = send(a, first, {"uid": 0}, thread, fields["file_id"], key, msg_type=1)
    require(sent.get("error") == 0, "Group image message rejected")
    repeated = send(a, first, {"uid": 0}, thread, fields["file_id"], key, msg_type=1)
    require(repeated["chat_datas"][0]["message_id"] == sent["chat_datas"][0]["message_id"])
    require(
        send(a, first, {"uid": 0}, other_thread, fields["file_id"], msg_type=1).get("error") != 0,
        "Image authorized for another group",
    )
    require(
        send(a, first, second, private_thread, fields["file_id"], msg_type=1).get("error") != 0,
        "Group resource reused as private authorization",
    )
    private_image = cluster.upload(first, second, b"private-only-image")
    require(
        send(a, first, {"uid": 0}, thread, private_image, msg_type=1).get("error") != 0,
        "Private image reused without group upload authorization",
    )
    b.request(1023, {})
    require(
        any(
            mid == 1019
            and body.get("thread_id") == thread
            and any(
                m.get("msg_type") == 1 and m.get("media_size") == len(data)
                for m in body.get("chat_datas", [])
            )
            for mid, body in b.notifications
        ),
        "Cross-node group image notification missing",
    )
    b.close()
    require(send(a, first, {"uid": 0}, thread, fields["file_id"], msg_type=1).get("error") == 0)
    b = cluster.login(second, "chat2")
    records = history(b, thread)
    require(
        len(records) == 2 and all(m["msg_type"] == 1 for m in records), "Offline group image lost"
    )
    pending = metadata(second, thread, b"xx")
    uploader = cluster.resource(second)
    require(uploader.request(1050, pending).get("ok"))
    require(uploader.request(1052, dict(pending, offset=0, data="eA==")).get("ok"))
    require(change(a, "remove", thread, member_uid=second["uid"]).get("error") == 0)
    require(not read(cluster, second, fields).get("ok"), "Removed member retained read permission")
    require(not uploader.request(1050, pending).get("ok"), "Removed uploader resumed")
    require(
        not uploader.request(1052, dict(pending, offset=1, data="eA==")).get("ok"),
        "Removed uploader wrote another chunk",
    )
    require(change(a, "add", thread, member_uid=second["uid"]).get("error") == 0)
    require(change(a, "transfer", thread, member_uid=second["uid"]).get("error") == 0)
    require(change(a, "leave", thread).get("error") == 0)
    require(not read(cluster, first, fields).get("ok"), "Former uploader bypassed membership")
    require(read(cluster, second, fields).get("ok"), "Remaining member lost original resource")
    require(change(b, "disband", thread).get("error") == 0)
    require(
        not read(cluster, second, fields).get("ok"), "Disbanded group resource remained accessible"
    )


def test_group_media_authority_failover_and_unavailable_fail_closed(recovery_cluster):
    cluster = recovery_cluster
    first, second = cluster.register(), cluster.register()
    a, b = cluster.login(first, "chat1"), cluster.login(second, "chat2")
    cluster.friends(first, second, a, b)
    thread = create(a, [second["uid"]])["group"]["thread_id"]
    fields = upload(cluster, first, thread, b"authority-failover-fixture")
    cluster.stop(cluster.processes["ChatServer"])
    require(read(cluster, second, fields).get("ok"), "Healthy second authority was not used")
    cluster.stop(cluster.processes["ChatServer2"])
    # Keep this isolated fixture token valid so denial must come from unavailable RPC authority.
    cluster.redis.setex("resource_token_" + str(second["uid"]), 60, second["token"])
    require(not read(cluster, second, fields).get("ok"), "Authority failure allowed group access")
