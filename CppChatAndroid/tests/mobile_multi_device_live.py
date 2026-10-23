"""Explicitly designated test accounts only; credentials come from environment."""

import base64
import configparser
import hashlib
import json
import os
from pathlib import Path
import socket
import ssl
import struct
import time
import urllib.request
import uuid


class Peer:
    def __init__(self, host, port):
        self.socket = ssl.create_default_context().wrap_socket(
            socket.create_connection((host, int(port)), timeout=12),
            server_hostname=host,
        )
        self.socket.settimeout(12)
        self.notifications = []

    def close(self):
        self.socket.close()

    def exact(self, count):
        result = bytearray()
        while len(result) < count:
            part = self.socket.recv(count - len(result))
            if not part:
                raise RuntimeError("TLS peer closed")
            result.extend(part)
        return bytes(result)

    def request(self, mid, body, resource=False):
        correlation = uuid.uuid4().hex
        data = json.dumps(
            dict(body, request_id=correlation), separators=(",", ":")
        ).encode()
        self.socket.sendall(struct.pack("!HH", mid, len(data)) + data)
        for _ in range(1000):
            kind, length = struct.unpack("!HH", self.exact(4))
            if length < 1 or length > (60 * 1024 if resource else 16 * 1024):
                raise RuntimeError("Invalid protocol length")
            response = json.loads(self.exact(length))
            if kind == mid + 1:
                if not resource and response.get("request_id") != correlation:
                    raise RuntimeError("Response correlation mismatch")
                return response
            self.notifications.append((kind, response))
        raise RuntimeError("Response not received")


def check(value, label):
    if not value:
        raise RuntimeError(label)


def main():
    config = configparser.ConfigParser()
    check(config.read(os.environ["CPPCHAT_TEST_CONFIG"]), "Config unavailable")
    gate = config["GateServer"]
    host = gate["host"]
    check(gate.get("scheme") == "https", "TLS gateway required")
    origin = "https://" + host + ":" + gate.get("port", "443")
    resource_host = config["ResourceServer"]["host"]
    resource_port = config["ResourceServer"]["port"]
    report = {"status": "RUNNING", "checks": []}
    peers = []

    def login(email, password, device):
        request = urllib.request.Request(
            origin + "/user_login",
            data=json.dumps({"email": email, "passwd": password}).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=20) as response:
            allocation = json.load(response)
        check(allocation.get("error") == 0, "HTTP auth failed")
        peer = Peer(allocation["host"], allocation["port"])
        peers.append(peer)
        credentials = {"uid": allocation["uid"], "token": allocation["token"]}
        authenticated = peer.request(1005, dict(credentials, device=device))
        check(authenticated.get("error") == 0, "Chat auth failed")
        check(
            authenticated.get("multi_device") is True, "Multi-device capability absent"
        )
        return peer, credentials

    def heartbeat(peer):
        check(peer.request(1023, {}).get("error") == 0, "Heartbeat failed")

    def send(peer, sender, recipient, thread, content, kind=0):
        reply = peer.request(
            1017,
            {
                "fromuid": sender["uid"],
                "touid": recipient["uid"],
                "thread_id": thread,
                "text_array": [
                    {
                        "unique_id": uuid.uuid4().hex,
                        "content": content,
                        "msg_type": kind,
                    }
                ],
            },
        )
        check(reply.get("error") == 0, "Message acknowledgement failed")

    def received(peer, content):
        heartbeat(peer)
        check(
            any(
                mid == 1019
                and any(
                    item.get("content") == content
                    for item in body.get("chat_datas", [])
                )
                for mid, body in peer.notifications
            ),
            "Live message absent",
        )

    def resource():
        peer = Peer(resource_host, resource_port)
        peers.append(peer)
        return peer

    try:
        desktop, first = login(
            os.environ["CPPCHAT_TEST_EMAIL_A"],
            os.environ["CPPCHAT_TEST_PASSWORD"],
            "desktop",
        )
        phone, mobile = login(
            os.environ["CPPCHAT_TEST_EMAIL_A"],
            os.environ["CPPCHAT_TEST_PASSWORD"],
            "android",
        )
        other, second = login(
            os.environ["CPPCHAT_TEST_EMAIL_B"],
            os.environ["CPPCHAT_TEST_PASSWORD"],
            "desktop",
        )
        check(
            first["uid"] == mobile["uid"] != second["uid"], "Unexpected test identities"
        )
        heartbeat(desktop)
        heartbeat(phone)
        report["checks"].append("same-account-desktop-android-online")
        thread = desktop.request(1027, {"uid": first["uid"], "other_id": second["uid"]})
        check(thread.get("error") == 0, "Test accounts must already be friends")
        thread_id = thread["thread_id"]
        marker = "CPPCHAT_ANDROID_FOUNDATION_" + uuid.uuid4().hex
        send(phone, first, second, thread_id, marker)
        received(desktop, marker)
        received(other, marker)
        reply_marker = marker + "_REPLY"
        send(other, second, first, thread_id, reply_marker)
        received(phone, reply_marker)
        received(desktop, reply_marker)
        report["checks"].extend(
            ["phone-send-ack-and-desktop-self-sync", "incoming-fanout-both-devices"]
        )
        # Synthetic PNG; no user attachment is read or uploaded.
        data = base64.b64decode(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
        )
        file_id = uuid.uuid4().hex
        image = dict(
            mobile,
            file_id=file_id,
            recipient=second["uid"],
            size=len(data),
            sha256=hashlib.sha256(data).hexdigest(),
        )
        upload = resource()
        check(upload.request(1050, image, True).get("ok"), "Phone resource init failed")
        check(
            upload.request(
                1052, dict(image, offset=0, data=base64.b64encode(data).decode()), True
            ).get("ok"),
            "Phone resource upload failed",
        )
        for credentials in (first, mobile, second):
            downloaded = resource().request(
                1054, dict(credentials, file_id=file_id, offset=0), True
            )
            check(downloaded.get("ok"), "Independent resource credential rejected")
            check(
                hashlib.sha256(base64.b64decode(downloaded["data"])).hexdigest()
                == image["sha256"],
                "Image SHA256 mismatch",
            )
        send(phone, first, second, thread_id, file_id, 1)
        received(desktop, file_id)
        received(other, file_id)
        report["checks"].append("both-device-resource-tokens-and-image-sha256")
        phone.close()
        deadline = time.monotonic() + 15
        revoked = False
        while time.monotonic() < deadline:
            candidate = resource()
            response = candidate.request(
                1054, dict(mobile, file_id=file_id, offset=0), True
            )
            candidate.close()
            if not response.get("ok"):
                revoked = True
                break
            time.sleep(0.2)
        check(revoked, "Closed phone credential not revoked")
        heartbeat(desktop)
        check(
            resource()
            .request(1054, dict(first, file_id=file_id, offset=0), True)
            .get("ok"),
            "Phone logout revoked desktop resource credential",
        )
        report["checks"].append("phone-logout-preserves-desktop-session-and-token")
        report["status"] = "PASS"
    finally:
        for peer in peers:
            peer.close()
        destination = Path(os.environ["CPPCHAT_TEST_REPORT"])
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
