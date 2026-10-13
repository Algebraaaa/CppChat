from datetime import datetime, timedelta, timezone
import ipaddress
import select
import socket
import socketserver
import ssl
import threading

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID


def certificates(directory, expired=False, untrusted=False, hostname_mismatch=False):
    directory.mkdir(parents=True, exist_ok=True)
    now = datetime.now(timezone.utc)

    def name(value):
        return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, value)])

    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    ca = (
        x509.CertificateBuilder()
        .subject_name(name("CppChat test CA"))
        .issuer_name(name("CppChat test CA"))
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(days=2))
        .not_valid_after(now + timedelta(days=2))
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .sign(key, hashes.SHA256())
    )
    trusted = ca
    if untrusted:
        wrong_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        trusted = (
            x509.CertificateBuilder()
            .subject_name(name("Other CA"))
            .issuer_name(name("Other CA"))
            .public_key(wrong_key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now - timedelta(days=2))
            .not_valid_after(now + timedelta(days=2))
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
            .sign(wrong_key, hashes.SHA256())
        )
    server_key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    server = (
        x509.CertificateBuilder()
        .subject_name(name("localhost"))
        .issuer_name(ca.subject)
        .public_key(server_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - timedelta(days=2))
        .not_valid_after(now - timedelta(days=1) if expired else now + timedelta(days=1))
        .add_extension(
            x509.SubjectAlternativeName(
                [x509.DNSName("different.example.test")]
                if hostname_mismatch
                else [x509.DNSName("localhost"), x509.IPAddress(ipaddress.ip_address("127.0.0.1"))]
            ),
            critical=False,
        )
        .sign(key, hashes.SHA256())
    )
    for file, data in (
        ("ca.pem", trusted.public_bytes(serialization.Encoding.PEM)),
        ("server.pem", server.public_bytes(serialization.Encoding.PEM)),
        (
            "server.key",
            server_key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            ),
        ),
    ):
        (directory / file).write_bytes(data)
    return directory / "ca.pem", directory / "server.pem", directory / "server.key"


class TLSProxy:
    def __init__(self, certificate, key, upstream):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(certificate, key)

        class Handler(socketserver.BaseRequestHandler):
            def handle(self):
                try:
                    self.request.settimeout(5)
                    with (
                        context.wrap_socket(self.request, server_side=True) as downstream,
                        socket.create_connection(upstream, timeout=5) as remote,
                    ):
                        while True:
                            readable, _, _ = select.select([downstream, remote], [], [], 5)
                            if not readable:
                                return
                            for source in readable:
                                data = source.recv(65536)
                                if not data:
                                    return
                                (remote if source is downstream else downstream).sendall(data)
                except (OSError, ssl.SSLError):
                    pass

        class Server(socketserver.ThreadingTCPServer):
            daemon_threads = True

        self.server = Server(("127.0.0.1", 0), Handler)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
