import json
import struct
import pytest
from hypothesis import given, settings, strategies as st
from harness.checks import require
from harness.protocol import (
    CHAT_LIMIT,
    RESOURCE_LIMIT,
    CHUNK_SIZE,
    IMAGE_LIMIT,
    encode,
    decode_header,
)


@pytest.mark.smoke
@pytest.mark.feature("PROTO-FRAME")
@pytest.mark.parametrize("limit", [CHAT_LIMIT, RESOURCE_LIMIT])
@pytest.mark.parametrize("delta", [-1, 0, 1])
def test_frame_boundary(limit, delta):
    payload = "x" * (limit + delta - 2)  # JSON quotes consume two bytes.
    if delta > 0:
        with pytest.raises(ValueError):
            encode(1017, payload, limit)
    else:
        packet = encode(1017, payload, limit)
        require(decode_header(packet[:4], limit) == (1017, limit + delta))
        require(json.loads(packet[4:]) == payload)


@pytest.mark.smoke
@pytest.mark.parametrize("length", [0, CHAT_LIMIT + 1, 65535])
def test_reject_header_lengths(length):
    with pytest.raises(ValueError):
        decode_header(struct.pack("!HH", 1005, length))


@pytest.mark.nightly
@pytest.mark.feature("PROTO-RANDOM")
@settings(max_examples=200, deadline=None)
@given(st.text(max_size=1000))
def test_unicode_payload_roundtrip(text):
    packet = encode(1017, {"content": text})
    _, size = decode_header(packet[:4])
    require(size == len(packet) - 4)
    require(json.loads(packet[4:])["content"] == text)


def test_documented_limits():
    require(
        (CHAT_LIMIT, RESOURCE_LIMIT, CHUNK_SIZE, IMAGE_LIMIT) == (16384, 61440, 32768, 20971520)
    )
