"""Owns all test state; never reads .runtime or a production configuration."""

import base64
import configparser
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import uuid

import pymysql
import redis
import requests
from testcontainers.core.container import DockerContainer

from .checks import eventually, require
from .protocol import Peer, RESOURCE_LIMIT, CHUNK_SIZE

ROOT = Path(__file__).resolve().parents[2]
IMAGES = {
    "mysql": "mysql:8.4.0",
    "redis": "redis:7.4.2-alpine",
    "mail": "axllent/mailpit:v1.21.8",
    "proxy": "ghcr.io/shopify/toxiproxy:2.9.0",
}


def free_port():
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


def executable(build, name, configuration="Debug"):
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [
        build / "bin" / name / configuration / (name + suffix),
        build / "bin" / name / (name + suffix),
        build / "bin" / (name + suffix),
        build / name / (name + suffix),
        build / "tests" / "cpp" / configuration / (name + suffix),
        build / "tests" / "cpp" / (name + suffix),
    ]
    for path in candidates:
        if path.is_file():
            return path.resolve()
    raise FileNotFoundError(f"Build target {name} in {build} ({configuration})")


def qt_environment(build):
    environment = dict(os.environ)
    cache = Path(build) / "CMakeCache.txt"
    extra = []
    if os.name == "nt" and cache.exists():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("Qt6_DIR:"):
                extra.append(str(Path(line.split("=", 1)[1]).parents[2] / "bin"))
            if line.startswith("CMAKE_CXX_COMPILER:"):
                extra.append(str(Path(line.split("=", 1)[1]).parent))
    environment["PATH"] = os.pathsep.join(extra + [environment.get("PATH", "")])
    return environment


def redact(text, credentials=()):
    for value in sorted(credentials, key=len, reverse=True):
        if value:
            text = text.replace(value, "<redacted>")
    text = re.sub(r"[\w.+-]+@[\w.-]+", "<email>", text)
    return re.sub(
        r"(?i)(token|passwd|password|verifycode)([\s:=\"]+)[^\s,\"}]+", r"\1\2<redacted>", text
    )


class Cluster:
    def __init__(self, directory, build, configuration="Debug"):
        self.directory, self.build = Path(directory).resolve(), Path(build).resolve()
        self.configuration = configuration
        self.stack = ExitStack()
        self.password = secrets.token_hex(24)
        self.root_password = secrets.token_hex(24)
        self.redis_password = secrets.token_hex(24)
        self.username = "test_" + secrets.token_hex(6)
        self.credentials = [self.password, self.root_password, self.redis_password]
        self.processes = {}
        self.accounts = []
        self.peers = []
        self.ports = {}

    def __enter__(self):
        try:
            self.start()
            return self
        except BaseException:
            self.__exit__(None, None, None)
            raise

    def __exit__(self, *_):
        for peer in self.peers:
            peer.close()
        for name in IMAGES:
            container = getattr(
                self, {"redis": "redis_container", "proxy": "proxy"}.get(name, name), None
            )
            if container is not None:
                try:
                    logs = b"".join(container.get_logs()).decode("utf-8", errors="replace")
                    (self.directory / (name + "-container.log")).write_text(
                        redact(logs, self.credentials), encoding="utf-8"
                    )
                except Exception:
                    pass  # The original setup error still determines the test result.
        self.stack.close()
        # Collect sanitized service logs; no configuration/credential artifacts.
        for log in self.directory.rglob("*.raw"):
            log.with_suffix(".log").write_text(
                redact(log.read_text(encoding="utf-8", errors="replace"), self.credentials),
                encoding="utf-8",
            )
            log.unlink()
        for log in self.directory.rglob("*.log"):
            log.write_text(
                redact(log.read_text(encoding="utf-8", errors="replace"), self.credentials),
                encoding="utf-8",
            )
        for config in self.directory.rglob("config.*"):
            if config.suffix in (".ini", ".json"):
                config.unlink()
        resources = (self.directory / "ResourceServer/images").resolve()
        if resources.exists():
            require(
                resources.is_relative_to(self.directory.resolve()),
                "Resource cleanup escaped test directory",
            )
            shutil.rmtree(resources)

    def container(self, name, ports, environment=None, command=None):
        item = DockerContainer(IMAGES[name]).with_exposed_ports(*ports)
        if os.name == "nt":
            # Docker Desktop's Linux port allocator cannot see native Windows listeners.
            for port in ports:
                item.with_bind_ports(port, free_port())
        item.with_kwargs(
            labels={
                "cppchat.regression": self.directory.name,
                "cppchat.regression.run": os.environ.get("CPPCHAT_TEST_RUN", self.directory.name),
            }
        )
        for key, value in (environment or {}).items():
            item.with_env(key, value)
        if command:
            item.with_command(command)
        # Register BEFORE starting so partially started containers also get removed.
        self.stack.callback(item.stop)
        item.start()
        return item

    @staticmethod
    def endpoint(item, port):
        if os.environ.get("CPPCHAT_TEST_CONTAINER_NETWORK") == "bridge":
            wrapped = item.get_wrapped_container()
            wrapped.reload()
            networks = wrapped.attrs["NetworkSettings"]["Networks"]
            address = next(iter(networks.values()))["IPAddress"]
            require(address, "Container has no assigned bridge address")
            return address, port
        return item.get_container_host_ip(), int(item.get_exposed_port(port))

    def start(self):
        self.directory.mkdir(parents=True, exist_ok=True)
        # Check binaries first, before pulling images or allocating dependencies.
        self.binaries = {
            name: executable(self.build, name, self.configuration)
            for name in (
                "GateServer",
                "StatusServer",
                "ChatServer",
                "ChatServer2",
                "ResourceServer",
            )
        }
        self.mysql = self.container(
            "mysql",
            [3306],
            {
                "MYSQL_ROOT_PASSWORD": self.root_password,
                "MYSQL_DATABASE": "cppchat",
                "MYSQL_USER": self.username,
                "MYSQL_PASSWORD": self.password,
            },
        )
        self.mysql_host, self.mysql_port = self.endpoint(self.mysql, 3306)
        host = self.mysql_host

        def mysql_ready():
            try:
                with self.sql(root=True) as db:
                    with db.cursor() as cursor:
                        cursor.execute("SELECT 1")
                return True
            except pymysql.MySQLError:
                return False

        eventually(mysql_ready, 120, "isolated MySQL")
        self.initialize_schema()
        # Match deployed services: schema setup uses root, business code only DML.
        with self.sql(root=True) as db:
            with db.cursor() as cursor:
                cursor.execute(f"REVOKE ALL PRIVILEGES, GRANT OPTION FROM '{self.username}'@'%'")
                cursor.execute(
                    f"GRANT SELECT, INSERT, UPDATE, DELETE ON cppchat.* TO '{self.username}'@'%'"
                )
        self.redis_container = self.container(
            "redis",
            [6379],
            command=f"redis-server --requirepass {self.redis_password} --save '' --appendonly no",
        )
        redis_host, self.redis_port = self.endpoint(self.redis_container, 6379)
        self.redis = redis.Redis(
            host=redis_host,
            port=self.redis_port,
            password=self.redis_password,
            decode_responses=True,
            socket_timeout=3,
        )
        eventually(
            lambda: self.redis.ping(),
            description="isolated Redis",
            retry_on=(redis.exceptions.ConnectionError, redis.exceptions.TimeoutError),
        )
        self.stack.callback(self.redis.close)
        self.mail = self.container("mail", [1025, 8025])
        mail_host, mail_port = self.endpoint(self.mail, 8025)
        smtp_host, smtp_port = self.endpoint(self.mail, 1025)
        self.mail_url = f"http://{mail_host}:{mail_port}"
        eventually(
            lambda: requests.get(self.mail_url + "/api/v1/messages", timeout=2).ok,
            description="Mailpit API",
        )
        # A real loopback fault proxy exposes separate MySQL and Redis ports.
        self.proxy = self.container("proxy", [8474, 8666, 8667])
        proxy_host, proxy_port = self.endpoint(self.proxy, 8474)
        self.proxy_url = f"http://{proxy_host}:{proxy_port}"
        eventually(
            lambda: requests.get(self.proxy_url + "/version", timeout=2).ok,
            description="Toxiproxy API",
        )
        for name, upstream, listener in (
            ("mysql", self.mysql, 8666),
            ("redis", self.redis_container, 8667),
        ):
            networks = upstream.get_wrapped_container().attrs["NetworkSettings"]["Networks"]
            address = next(iter(networks.values()))["IPAddress"]
            port = 3306 if name == "mysql" else 6379
            response = requests.post(
                self.proxy_url + "/proxies",
                json={
                    "name": name,
                    "listen": f"0.0.0.0:{listener}",
                    "upstream": f"{address}:{port}",
                },
                timeout=5,
            )
            response.raise_for_status()
        _, self.sql_service_port = self.endpoint(self.proxy, 8666)
        _, self.redis_service_port = self.endpoint(self.proxy, 8667)
        for name in (
            "gate",
            "verify",
            "status",
            "chat1",
            "chat2",
            "rpc1",
            "rpc2",
            "media1",
            "media2",
            "resource",
        ):
            self.ports[name] = free_port()
        # No defaults from checked-out developer configs are inherited.
        config = configparser.ConfigParser()
        config.optionxform = str
        config.read_dict(
            {
                "GateServer": {
                    "Host": "127.0.0.1",
                    "Port": str(self.ports["gate"]),
                    "TimeoutSeconds": "15",
                },
                "VerifyServer": {"Host": "127.0.0.1", "Port": str(self.ports["verify"])},
                "StatusServer": {"Host": "127.0.0.1", "Port": str(self.ports["status"])},
                "Mysql": {
                    "Host": proxy_host,
                    "Port": str(self.sql_service_port),
                    "User": self.username,
                    "Passwd": self.password,
                    "Schema": "cppchat",
                    "PoolSize": "3",
                    "BorrowTimeoutSeconds": "1",
                    "ConnectTimeoutSeconds": "1",
                    "ReadTimeoutSeconds": "2",
                    "WriteTimeoutSeconds": "2",
                },
                "Redis": {
                    "Host": proxy_host,
                    "Port": str(self.redis_service_port),
                    "Passwd": self.redis_password,
                    "PoolSize": "3",
                },
                "ChatServer1": {
                    "Name": "ChatServer",
                    "Host": "127.0.0.1",
                    "Port": str(self.ports["chat1"]),
                },
                "ChatServer2": {
                    "Name": "ChatServer2",
                    "Host": "127.0.0.1",
                    "Port": str(self.ports["chat2"]),
                },
                "Business": {"Workers": "4", "QueueLimit": "256", "MaxConnections": "512"},
            }
        )
        for name, node, peer, tcp, rpc, peer_rpc in (
            ("ChatServer", "ChatServer", "ChatServer2", "chat1", "rpc1", "rpc2"),
            ("ChatServer2", "ChatServer2", "ChatServer", "chat2", "rpc2", "rpc1"),
        ):
            config["SelfServer"] = {
                "Name": node,
                "Host": "127.0.0.1",
                "Port": str(self.ports[tcp]),
                "RPCPort": str(self.ports[rpc]),
            }
            config["PeerServer"] = {"Servers": "Peer"}
            config["MediaAuthorization"] = {
                "Port": str(self.ports["media1" if name == "ChatServer" else "media2"])
            }
            config["Peer"] = {"Name": peer, "Host": "127.0.0.1", "Port": str(self.ports[peer_rpc])}
            self.write_config(name, config)
        for name in ("StatusServer", "GateServer"):
            self.write_config(name, config)
        verify_dir = self.directory / "VerifyServer"
        verify_dir.mkdir()
        verify_config = {
            "listen": f"127.0.0.1:{self.ports['verify']}",
            "email": {
                "user": "sender@example.test",
                "pass": "",
                "host": smtp_host,
                "port": smtp_port,
                "secure": False,
                "auth": False,
            },
            "mysql": {"host": host, "port": self.mysql_port},
            "redis": {
                "host": proxy_host,
                "port": self.redis_service_port,
                "password": self.redis_password,
            },
        }
        verify_path = verify_dir / "config.json"
        verify_path.write_text(json.dumps(verify_config), encoding="utf-8")
        self.spawn(
            "VerifyServer",
            [shutil.which("node") or "node", str(ROOT / "VerifyServer/server.js")],
            {"CPPCHAT_VERIFY_CONFIG": str(verify_path)},
        )
        self.spawn("StatusServer")
        self.spawn("ChatServer")
        self.spawn("ChatServer2")
        resource_env = {
            "CPPCHAT_MEDIA_AUTH_PORTS": str(self.ports["media1"]) + "," + str(self.ports["media2"]),
            "CPPCHAT_RESOURCE_LISTEN": "127.0.0.1",
            "CPPCHAT_RESOURCE_PORT": str(self.ports["resource"]),
            "CPPCHAT_RESOURCE_DIR": str(self.directory / "ResourceServer/images"),
            "CPPCHAT_REDIS_HOST": proxy_host,
            "CPPCHAT_REDIS_PORT": str(self.redis_service_port),
            "CPPCHAT_REDIS_PASSWORD": self.redis_password,
        }
        self.spawn("ResourceServer", env=resource_env)

        def listening(port, name):
            require(self.processes[name].poll() is None, f"{name} exited before readiness")
            with socket.create_connection(("127.0.0.1", port), timeout=1):
                return True

        for port, name in (
            ("verify", "VerifyServer"),
            ("status", "StatusServer"),
            ("rpc1", "ChatServer"),
            ("rpc2", "ChatServer2"),
            ("resource", "ResourceServer"),
        ):
            eventually(lambda: listening(self.ports[port], name), 30, name + " listening")
        eventually(
            lambda: self.redis.exists("chat_node_ChatServer")
            and self.redis.exists("chat_node_ChatServer2"),
            45,
            "both real chat-node leases",
        )
        self.spawn("GateServer")
        self.gate_url = f"http://127.0.0.1:{self.ports['gate']}"
        eventually(
            lambda: requests.get(self.gate_url + "/get_test", timeout=2).ok, description="Gate HTTP"
        )
        (self.directory / "versions.json").write_text(
            json.dumps(
                {
                    "images": IMAGES,
                    "image_ids": {
                        name: item.get_wrapped_container().image.id
                        for name, item in (
                            ("mysql", self.mysql),
                            ("redis", self.redis_container),
                            ("mail", self.mail),
                            ("proxy", self.proxy),
                        )
                    },
                    "binaries": {
                        name: hashlib.sha256(path.read_bytes()).hexdigest()
                        for name, path in self.binaries.items()
                    },
                    "ports": self.ports,
                    "node": subprocess.check_output(
                        [shutil.which("node") or "node", "--version"], text=True
                    ).strip(),
                }
            ),
            encoding="utf-8",
        )

    def sql(self, root=False):
        return pymysql.connect(
            host=self.mysql_host,
            port=self.mysql_port,
            user="root" if root else self.username,
            password=self.root_password if root else self.password,
            database="cppchat",
            autocommit=True,
            connect_timeout=2,
            read_timeout=5,
            write_timeout=5,
        )

    def initialize_schema(self):
        schema = ROOT / "database/mysql/schema.sql"
        from pymysql.constants import CLIENT

        with pymysql.connect(
            host=self.mysql_host,
            port=self.mysql_port,
            user="root",
            password=self.root_password,
            client_flag=CLIENT.MULTI_STATEMENTS,
            autocommit=True,
        ) as db:
            with db.cursor() as cursor:
                cursor.execute(schema.read_text(encoding="utf-8-sig"))
                while cursor.nextset():
                    pass

    def write_config(self, name, config):
        directory = self.directory / name
        directory.mkdir(parents=True, exist_ok=True)
        with (directory / "config.ini").open("w", encoding="utf-8") as file:
            config.write(file)

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)

    def spawn(self, name, command=None, env=None):
        directory = self.directory / name
        directory.mkdir(parents=True, exist_ok=True)
        output = self.stack.enter_context(
            (directory / f"output-{uuid.uuid4().hex[:8]}.raw").open("wb")
        )
        process = subprocess.Popen(
            command or [str(self.binaries[name])],
            cwd=directory,
            env=dict(os.environ, **(env or {})),
            stdout=output,
            stderr=subprocess.STDOUT,
        )
        self.stack.callback(self.stop, process)
        self.register_process(name, process)
        return process

    def register_process(self, name, process):
        self.processes[name] = process
        import psutil

        manifest = self.directory / "owned-processes.json"
        existing = json.loads(manifest.read_text(encoding="utf-8")) if manifest.exists() else []
        existing.append(
            {"pid": process.pid, "name": name, "created": psutil.Process(process.pid).create_time()}
        )
        manifest.write_text(json.dumps(existing), encoding="utf-8")

    def post(self, route, data):
        response = requests.post(self.gate_url + route, json=data, timeout=20)
        require(response.status_code == 200, f"HTTP status {response.status_code}")
        return response.json()

    def code(self, email):
        response = self.post("/get_verifycode", {"email": email})
        require(response.get("error") == 0, "Verification service rejected request")

        def find_mail():
            messages = requests.get(self.mail_url + "/api/v1/messages", timeout=3).json()[
                "messages"
            ]
            for message in messages:
                if any(recipient.get("Address") == email for recipient in message.get("To", [])):
                    return requests.get(
                        self.mail_url + "/api/v1/message/" + message["ID"], timeout=3
                    ).json()
            return None

        mail = eventually(find_mail, 20, "actual verification email")
        match = re.search(r"\b[1-9][0-9]{5}\b", mail.get("Text", ""))
        require(match is not None, "Email lacks six-digit code")
        code = match.group()
        require(self.redis.get("code_" + email) == code, "SMTP code differs from Redis")
        self.credentials.append(code)
        return code

    def register(self):
        identity = secrets.token_hex(8)
        account = {
            "user": "User" + identity,
            "email": identity + "@example.test",
            "passwd": secrets.token_hex(16),
        }
        self.credentials.append(account["passwd"])
        reply = self.post(
            "/user_register",
            dict(account, confirm=account["passwd"], verifycode=self.code(account["email"])),
        )
        require(reply.get("error") == 0, "Registration failed")
        account["uid"] = reply["uid"]
        self.accounts.append(account)
        return account

    def login(self, account, node=None):
        reply = self.post("/user_login", {"email": account["email"], "passwd": account["passwd"]})
        require(reply.get("error") == 0, "HTTP login failed")
        peer = Peer("127.0.0.1", self.ports[node] if node else int(reply["port"]))
        self.peers.append(peer)
        logged_in = peer.request(1005, {"uid": account["uid"], "token": reply["token"]})
        require(logged_in.get("error") == 0, "Chat login failed")
        account["token"] = logged_in["token"]
        self.credentials.append(account["token"])
        return peer

    def friends(self, first, second, a, b):
        require(
            a.request(
                1009,
                {
                    "uid": first["uid"],
                    "touid": second["uid"],
                    "applyname": "hello",
                    "bakname": "friend",
                },
            ).get("error")
            == 0,
            "Friend apply failed",
        )
        require(
            b.request(
                1013, {"fromuid": second["uid"], "touid": first["uid"], "back": "friend"}
            ).get("error")
            == 0,
            "Friend accept failed",
        )
        thread = a.request(1027, {"uid": first["uid"], "other_id": second["uid"]})
        require(thread.get("error") == 0, "Private conversation failed")
        return thread["thread_id"]

    def resource(self, account):
        peer = Peer("127.0.0.1", self.ports["resource"], RESOURCE_LIMIT)
        self.peers.append(peer)
        return peer

    def upload(self, first, second, data):
        file_id = uuid.uuid4().hex
        peer = self.resource(first)
        common = {"uid": first["uid"], "token": first["token"], "file_id": file_id}
        metadata = {
            "recipient": second["uid"],
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
        require(peer.request(1050, dict(common, **metadata)).get("ok"), "Upload init failed")
        for offset in range(0, len(data), CHUNK_SIZE):
            reply = peer.request(
                1052,
                dict(
                    common,
                    offset=offset,
                    data=base64.b64encode(data[offset : offset + CHUNK_SIZE]).decode(),
                ),
            )
            require(reply.get("ok"), "Upload chunk failed")
        return file_id

    def fault(self, name, enabled):
        response = requests.post(
            self.proxy_url + "/proxies/" + name, json={"enabled": enabled}, timeout=5
        )
        response.raise_for_status()
