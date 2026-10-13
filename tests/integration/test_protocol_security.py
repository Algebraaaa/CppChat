import json
import socket
import struct
import uuid
import pytest
from hypothesis import given, settings, strategies as st
from harness.checks import require
from harness.protocol import Peer, CHAT_LIMIT, encode

pytestmark = [pytest.mark.integration, pytest.mark.feature("PROTO-SECURITY")]


def test_unauthenticated_and_invalid_token(cluster):
    with Peer("127.0.0.1", cluster.ports["chat1"]) as peer:
        require(
            peer.request(1007, {"uid": "5"}).get("error") != 0, "Unauthenticated search accepted"
        )
        require(peer.request(1005, {"uid": 999999, "token": "invalid"}).get("error") != 0)


def test_split_coalesced_legacy_and_unknown_frames(cluster, pair):
    first, second, a, b, thread = pair
    require(a.request(1023, {}, fragmented=True).get("error") == 0)
    ids = [uuid.uuid4().hex, uuid.uuid4().hex]
    a.socket.sendall(b"".join(encode(1023, {"request_id": key}) for key in ids))
    received = []
    while len(received) < 2:
        message_id, body = a.receive()
        if message_id == 1024:
            received.append(body["request_id"])
        else:
            a.notifications.append((message_id, body))
    require(received == ids, "Coalesced requests reordered or lost")
    a.socket.sendall(encode(32000, {}))
    require(a.request(1023, {}).get("error") == 0, "Unknown message killed valid session")
    a.socket.sendall(encode(1023, {}))  # Legacy client omits correlation.
    while True:
        message_id, response = a.receive()
        if message_id == 1024:
            break
    require(response.get("error") == 0 and "request_id" not in response)


@pytest.mark.parametrize("size", [CHAT_LIMIT + 1, 65535])
def test_oversized_frame_disconnects(cluster, size):
    with socket.create_connection(("127.0.0.1", cluster.ports["chat1"]), timeout=5) as peer:
        peer.sendall(struct.pack("!HH", 1005, size))
        peer.settimeout(5)
        try:
            require(peer.recv(1) == b"", "Oversized frame not closed")
        except ConnectionResetError:
            pass


def test_malformed_json_and_actor_impersonation(cluster, pair):
    first, second, a, b, thread = pair
    a.socket.sendall(struct.pack("!HH", 1023, 1) + b"{")
    while True:
        message_id, reply = a.receive()
        if message_id == 1024:
            break
    require(reply.get("error") != 0)
    require(
        a.request(1025, {"uid": second["uid"]}).get("error") != 0, "Actor impersonation accepted"
    )
    require(a.request(1027, {"uid": second["uid"], "other_id": first["uid"]}).get("error") != 0)


@pytest.mark.parametrize("length", [CHAT_LIMIT - 1, CHAT_LIMIT])
def test_actual_chat_packet_limit(cluster, pair, length):
    first, second, a, b, thread = pair
    key = uuid.uuid4().hex
    body = {"request_id": key, "padding": ""}
    overhead = len(json.dumps(body, separators=(",", ":")).encode())
    body["padding"] = "x" * (length - overhead)
    packet = encode(1023, body)
    require(len(packet) == length + 4)
    a.socket.sendall(packet)
    while True:
        message_id, response = a.receive()
        if message_id == 1024:
            break
    require(response.get("error") == 0 and response.get("request_id") == key)


@pytest.mark.nightly
@settings(max_examples=30, deadline=None)
@given(st.integers(min_value=CHAT_LIMIT + 1, max_value=65535))
def test_generated_oversized_headers_on_real_server(cluster, size):
    with socket.create_connection(("127.0.0.1", cluster.ports["chat1"]), timeout=5) as peer:
        peer.sendall(struct.pack("!HH", 1005, size))
        peer.settimeout(5)
        try:
            require(peer.recv(1) == b"", "Generated oversized frame was not closed")
        except ConnectionResetError:
            pass
