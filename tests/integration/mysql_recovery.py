"""Linux regression tests with isolated MySQL schema and private service processes.

Requires local MySQL admin socket access and the project's Linux dependencies.
HTTP mode starts its own Redis, StatusServer and loopback-only GateServer.
"""

import argparse
import configparser
from contextlib import contextmanager, ExitStack
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request


PROCESS_TIMEOUT_SECONDS = 60
STARTUP_TIMEOUT_SECONDS = 15


def require(condition, message):
    """Checks remain active with python -O and PYTHONOPTIMIZE."""
    if not condition:
        raise AssertionError(message)


def mysql(query):
    result = subprocess.run(
        ["mysql", "--batch", "--skip-column-names"], input=query,
        text=True, capture_output=True, check=True, timeout=20,
    )
    return result.stdout.strip()


def write_config(path, sections):
    config = configparser.ConfigParser()
    config.optionxform = str
    config.read_dict(sections)
    with path.open("w", encoding="utf-8") as output:
        config.write(output)


def unused_ports(count):
    # 同时持有临时 socket，保证本轮选择的端口互不重复。
    with ExitStack() as reservations:
        ports = []
        for _ in range(count):
            reservation = reservations.enter_context(socket.socket())
            reservation.bind(("127.0.0.1", 0))
            ports.append(reservation.getsockname()[1])
        return ports


@contextmanager
def managed_process(command, directory, label):
    """Own the child and its log; never stop an existing service by port/PID."""
    with (directory / (label + ".log")).open("w") as log:
        child = subprocess.Popen(
            command, cwd=directory, stdout=log, stderr=subprocess.STDOUT,
        )
        try:
            yield child
        finally:
            if child.poll() is None:
                child.terminate()
            try:
                child.wait(timeout=10)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=5)


def wait_ready(child, probe, label):
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        require(child.poll() is None, label + " exited before becoming ready")
        try:
            probe()
            return
        except (OSError, urllib.error.URLError, subprocess.SubprocessError):
            time.sleep(0.1)
    raise RuntimeError(label + " did not become ready")


class DatabaseFixture:
    """Only this run's randomized database user/schema may be mutated."""

    def __init__(self):
        self.suffix = secrets.token_hex(6)
        self.schema = "mysql_recovery_" + self.suffix
        self.user = "recovery_" + self.suffix
        self.password = secrets.token_hex(20)
        self.blocked_password = secrets.token_hex(20)

    def create(self):
        mysql(
            f"CREATE DATABASE `{self.schema}`; "
            f"CREATE USER '{self.user}'@'127.0.0.1' "
            f"IDENTIFIED BY '{self.password}'; "
            f"GRANT SELECT, INSERT, UPDATE, DELETE ON `{self.schema}`.* "
            f"TO '{self.user}'@'127.0.0.1'; "
            f"CREATE TABLE `{self.schema}`.users LIKE cppchat.users;"
        )

    def cleanup(self):
        require(self.schema == "mysql_recovery_" + self.suffix,
                "Invalid test schema cleanup target")
        require(self.user == "recovery_" + self.suffix,
                "Invalid test user cleanup target")
        mysql(f"DROP DATABASE IF EXISTS `{self.schema}`; "
              f"DROP USER IF EXISTS '{self.user}'@'127.0.0.1';")

    def block(self):
        mysql(f"ALTER USER '{self.user}'@'127.0.0.1' "
              f"IDENTIFIED BY '{self.blocked_password}';")
        identifiers = mysql(
            f"SELECT ID FROM information_schema.PROCESSLIST WHERE USER='{self.user}';"
        )
        for identifier in identifiers.splitlines():
            require(identifier.isdigit(), "Invalid test connection ID")
            mysql(f"KILL {identifier};")

    def restore(self):
        mysql(f"ALTER USER '{self.user}'@'127.0.0.1' "
              f"IDENTIFIED BY '{self.password}';")

    def settings(self):
        return {
            "Host": "127.0.0.1", "Port": "3306", "User": self.user,
            "Passwd": self.password, "Schema": self.schema, "PoolSize": "1",
            "BorrowTimeoutSeconds": "1", "ConnectTimeoutSeconds": "3",
            "ReadTimeoutSeconds": "5", "WriteTimeoutSeconds": "5",
        }


def run_pool_tests(source, work, fixture):
    write_config(work / "config.ini", {"Mysql": fixture.settings()})
    environment = dict(
        os.environ, MYSQL_TEST_URL="tcp://127.0.0.1:3306",
        MYSQL_TEST_USER=fixture.user, MYSQL_TEST_PASSWORD=fixture.password,
        MYSQL_TEST_SCHEMA=fixture.schema,
    )
    for target in ("gate", "shared"):
        binary = work / target
        component = source / ("GateServer" if target == "gate" else "ChatServerShared")
        command = [
            "g++", "-std=c++17", "-O0", "-pthread", "-I" + str(component),
            "-I" + str(source / "ChatServerShared"),
            str(source / "tests/integration/mysql_pool_recovery.cpp"),
            str(source / "ChatServerShared/MySqlPool.cpp"),
        ]
        if target == "gate":
            command += [
                "-DTEST_GATE_POOL", str(component / "MysqlDao.cpp"),
                str(component / "ConfigMgr.cpp"), str(component / "Logger.cpp"),
            ]
        command += [
            "-lmysqlcppconn", "-lssl", "-lcrypto", "-lboost_filesystem",
            "-lboost_system", "-o", str(binary),
        ]
        subprocess.run(command, check=True, timeout=120)
        child = subprocess.Popen(
            [str(binary)], cwd=work, env=environment, text=True,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        watchdog = threading.Timer(PROCESS_TIMEOUT_SECONDS, child.kill)
        watchdog.start()
        try:
            for line in child.stdout:
                if line.startswith("CONTROL:"):
                    action = line.strip().split(":", 1)[1]
                    require(action in ("idle", "block", "restore"), "Invalid test action")
                    if action == "idle":
                        identifiers = mysql(f"SELECT ID FROM information_schema.PROCESSLIST WHERE USER='{fixture.user}';")
                        deadline = time.monotonic() + 10
                        while identifiers and time.monotonic() < deadline:
                            identifiers = mysql(f"SELECT ID FROM information_schema.PROCESSLIST WHERE USER='{fixture.user}';")
                            threading.Event().wait(0.05)
                        require(not identifiers, "Idle test session did not expire")
                    else:
                        (fixture.block if action == "block" else fixture.restore)()
                    child.stdin.write("continue\n")
                    child.stdin.flush()
                elif line.startswith(("PASS:", "FAIL:")):
                    print(target + " " + line.strip(), flush=True)
            require(child.wait(timeout=15) == 0, target + " regression failed")
        finally:
            watchdog.cancel()
            if child.poll() is None:
                child.kill()
            child.wait(timeout=10)
            child.stdin.close()
            child.stdout.close()
            fixture.restore()


def run_http_tests(gate_binary, status_binary, redis_server, work, fixture):
    redis_directory = work / "redis"
    status_directory = work / "status"
    gate_directory = work / "http"
    for directory in (redis_directory, status_directory, gate_directory):
        directory.mkdir()
    redis_port, status_port, gate_port, verify_port, node_port = unused_ports(5)
    redis_password = secrets.token_hex(20)
    redis_config = redis_directory / "redis.conf"
    redis_config.write_text(
        f"bind 127.0.0.1\nport {redis_port}\nrequirepass {redis_password}\n"
        'save ""\nappendonly no\ndaemonize no\n', encoding="utf-8",
    )
    redis_settings = {
        "Host": "127.0.0.1", "Port": str(redis_port),
        "Passwd": redis_password, "PoolSize": "2",
    }
    write_config(status_directory / "config.ini", {
        "StatusServer": {"Host": "127.0.0.1", "Port": str(status_port)},
        "Redis": redis_settings,
        "ChatServer1": {
            "Name": "FixtureNode", "Host": "127.0.0.1", "Port": str(node_port),
            "Enabled": "true",
        },
        "ChatServer2": {"Enabled": "false"},
    })
    write_config(gate_directory / "config.ini", {
        "GateServer": {"Host": "127.0.0.1", "Port": str(gate_port)},
        "StatusServer": {"Host": "127.0.0.1", "Port": str(status_port)},
        "VerifyServer": {"Host": "127.0.0.1", "Port": str(verify_port)},
        "Redis": redis_settings, "Mysql": fixture.settings(),
        "Business": {"Workers": "1", "IoWorkers": "1"},
    })

    def redis_command(*arguments):
        result = subprocess.run(
            ["redis-cli", "--no-auth-warning", "-h", "127.0.0.1",
             "-p", str(redis_port), *arguments],
            env=dict(os.environ, REDISCLI_AUTH=redis_password),
            text=True, capture_output=True, check=True, timeout=5,
        )
        require(not result.stdout.startswith(("ERR", "NOAUTH")),
                "Private Redis command failed")
        return result.stdout.strip()

    def post(route, body):
        request = urllib.request.Request(
            f"http://127.0.0.1:{gate_port}" + route,
            data=json.dumps(body).encode(),
            headers={"Content-Type": "application/json"},
        )
        with urllib.request.urlopen(request, timeout=15) as response:
            return json.load(response)

    def expect_error(route, body, expected):
        result = post(route, body)
        actual = result.get("error")
        require(actual == expected,
                f"{route}: expected error {expected}, received {actual}")
        return result

    name, email = "HttpRecovery", "recovery-" + fixture.suffix + "@example.test"
    old_password, new_password = "Initial123", "Updated123"
    salt = secrets.token_hex(16)
    digest = hashlib.pbkdf2_hmac(
        "sha256", old_password.encode(), bytes.fromhex(salt), 120000,
    ).hex()
    mysql(f"INSERT INTO `{fixture.schema}`.users "
          "(username, email, password_hash, password_salt, password_iterations) "
          f"VALUES ('{name}', '{email}', '{digest}', '{salt}', 120000);")
    code = f"{secrets.randbelow(1000000):06d}"
    code_key = "code_" + email
    reset = {"user": name, "email": email,
             "passwd": new_password, "verifycode": code}

    with managed_process([redis_server, str(redis_config)],
                         redis_directory, "redis") as redis_child:
        wait_ready(redis_child, lambda: require(redis_command("PING") == "PONG",
                                               "Private Redis is not ready"), "Redis")
        health = json.dumps({"instance_id": fixture.suffix, "online": 0})
        redis_command("SET", "chat_node_FixtureNode", health, "EX", "120")
        with managed_process([str(status_binary)], status_directory, "status") as status:
            def status_probe():
                with socket.create_connection(("127.0.0.1", status_port), timeout=1):
                    pass
            wait_ready(status, status_probe, "StatusServer")
            with managed_process([str(gate_binary)], gate_directory, "gate") as gate:
                wrong_login = {"email": email, "passwd": "Wrong123"}
                wait_ready(gate, lambda: post("/user_login", wrong_login), "GateServer")
                expect_error("/user_login", wrong_login, 1009)
                redis_command("SET", code_key, code, "EX", "120")
                expect_error("/reset_pwd", dict(reset, user="OtherUser"), 1007)
                fixture.block()
                expect_error("/reset_pwd", reset, 1014)
                expect_error("/user_login", {"email": email, "passwd": old_password}, 1014)
                fixture.restore()
                expect_error("/reset_pwd", reset, 0)
                require(redis_command("EXISTS", code_key) == "0",
                        "Successful reset did not consume the verification code")
                expect_error("/reset_pwd", reset, 1003)
                expect_error("/user_login", {"email": email, "passwd": old_password}, 1009)
                stored = mysql(f"SELECT password_hash, password_salt FROM "
                               f"`{fixture.schema}`.users WHERE email='{email}';")
                actual_hash, actual_salt = stored.split("\t")
                calculated = hashlib.pbkdf2_hmac(
                    "sha256", new_password.encode(), bytes.fromhex(actual_salt), 120000,
                ).hex()
                require(calculated == actual_hash, "Updated password hash is invalid")
                expect_error("/user_login", {"email": email, "passwd": new_password}, 0)
                print("http PASS: isolated services, mismatch=1007, wrong password=1009, "
                      "outage=1014, recovered reset/login=0, one-time code", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--gate-binary", type=Path)
    parser.add_argument("--status-binary", type=Path)
    parser.add_argument("--redis-server", default=shutil.which("redis-server"))
    args = parser.parse_args()
    if bool(args.gate_binary) != bool(args.status_binary):
        parser.error("HTTP tests require both --gate-binary and --status-binary")
    if args.gate_binary and not args.redis_server:
        parser.error("HTTP tests require redis-server")
    args.source = args.source.resolve()
    for binary in (args.gate_binary, args.status_binary):
        if binary and not binary.is_file():
            parser.error("HTTP service binary does not exist")
    os.umask(0o077)
    fixture = DatabaseFixture()
    try:
        fixture.create()
        with tempfile.TemporaryDirectory(prefix="cppchat-mysql-recovery-") as directory:
            work = Path(directory)
            run_pool_tests(args.source, work, fixture)
            if args.gate_binary:
                run_http_tests(args.gate_binary.resolve(), args.status_binary.resolve(),
                               args.redis_server, work, fixture)
    finally:
        fixture.cleanup()
        print("Private test services and database/user removed; "
              "production accounts/cache unchanged.", flush=True)


if __name__ == "__main__":
    main()
