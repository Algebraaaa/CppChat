import json
import socket
import struct
import uuid

CHAT_LIMIT = 16 * 1024
RESOURCE_LIMIT = 60 * 1024
CHUNK_SIZE = 32 * 1024
IMAGE_LIMIT = 20 * 1024 * 1024


def encode(message_id, body, limit=CHAT_LIMIT):
    data = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode()
    if not 0 < len(data) <= limit:
        raise ValueError("Payload exceeds protocol limit")
    return struct.pack("!HH", message_id, len(data)) + data


def decode_header(data, limit=CHAT_LIMIT):
    if len(data) != 4:
        raise ValueError("Incomplete header")
    message_id, length = struct.unpack("!HH", data)
    if not 0 < length <= limit:
        raise ValueError("Invalid payload size")
    return message_id, length


class Peer:
    def __init__(self, host, port, limit=CHAT_LIMIT):
        self.socket = socket.create_connection((host, int(port)), timeout=8)
        self.socket.settimeout(8)
        self.limit = limit
        self.notifications = []

    def close(self):
        self.socket.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

    def exact(self, length):
        chunks = bytearray()
        while len(chunks) < length:
            chunk = self.socket.recv(length - len(chunks))
            if not chunk:
                raise ConnectionError("Peer closed mid-frame")
            chunks.extend(chunk)
        return bytes(chunks)

    def receive(self):
        message_id, length = decode_header(self.exact(4), self.limit)
        return message_id, json.loads(self.exact(length))

    def request(self, message_id, body, fragmented=False):
        request_id = str(uuid.uuid4())
        body = dict(body, request_id=request_id)
        packet = encode(message_id, body, self.limit)
        if fragmented:
            for part in (packet[:1], packet[1:3], packet[3:7], packet[7:]):
                self.socket.sendall(part)
        else:
            self.socket.sendall(packet)
        for _ in range(1000):
            response_id, response = self.receive()
            if response_id == message_id + 1:
                # Resource v1 has no correlation field; chat must echo it.
                if self.limit == CHAT_LIMIT and response.get("request_id") != request_id:
                    raise AssertionError("Response correlation mismatch")
                return response
            self.notifications.append((response_id, response))
        raise AssertionError("Response lost behind notifications")


async def async_receive(reader, limit=CHAT_LIMIT):
    message_id, length = decode_header(await reader.readexactly(4), limit)
    return message_id, json.loads(await reader.readexactly(length))
