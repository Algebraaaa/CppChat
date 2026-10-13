"""Async resource load uses the same frames and bounds as functional tests."""

import asyncio
import base64
import hashlib
import time
import uuid
from harness.checks import require
from harness.protocol import CHUNK_SIZE, RESOURCE_LIMIT, async_receive, encode


async def transfer(port, sender, recipient, data):
    async def exchange(reader, writer, message_id, body):
        writer.write(encode(message_id, body, RESOURCE_LIMIT))
        await writer.drain()
        response_id, response = await asyncio.wait_for(async_receive(reader, RESOURCE_LIMIT), 10)
        require(
            response_id == message_id + 1 and response.get("ok"), "Resource load request rejected"
        )
        return response

    started = time.perf_counter()
    file_id = uuid.uuid4().hex
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    common = {"uid": sender["uid"], "token": sender["token"], "file_id": file_id}
    try:
        await exchange(
            reader,
            writer,
            1050,
            dict(
                common,
                recipient=recipient["uid"],
                size=len(data),
                sha256=hashlib.sha256(data).hexdigest(),
            ),
        )
        for offset in range(0, len(data), CHUNK_SIZE):
            await exchange(
                reader,
                writer,
                1052,
                dict(
                    common,
                    offset=offset,
                    data=base64.b64encode(data[offset : offset + CHUNK_SIZE]).decode(),
                ),
            )
    finally:
        writer.close()
        await writer.wait_closed()
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    common = dict(common, uid=recipient["uid"], token=recipient["token"])
    received = bytearray()
    try:
        while len(received) < len(data):
            reply = await exchange(reader, writer, 1054, dict(common, offset=len(received)))
            chunk = base64.b64decode(reply["data"], validate=True)
            require(chunk, "Resource load download stalled")
            received.extend(chunk)
    finally:
        writer.close()
        await writer.wait_closed()
    require(
        hashlib.sha256(received).digest() == hashlib.sha256(data).digest(),
        "Resource load checksum mismatch",
    )
    return (time.perf_counter() - started) * 1000
