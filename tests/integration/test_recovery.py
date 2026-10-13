import os
import queue
import subprocess
import threading
import uuid
import pytest
from harness.checks import eventually, require
from harness.cluster import executable

pytestmark = [pytest.mark.integration, pytest.mark.nightly]


@pytest.mark.feature("DB-POOL-RECOVERY")
@pytest.mark.parametrize("variant", ["shared", "gate"])
def test_mysql_recovery_controller(recovery_cluster, variant):
    cluster = recovery_cluster
    binary = executable(cluster.build, "mysql_pool_" + variant, cluster.configuration)
    environment = dict(
        os.environ,
        MYSQL_TEST_URL=f"tcp://{cluster.mysql_host}:{cluster.mysql_port}",
        MYSQL_TEST_USER=cluster.username,
        MYSQL_TEST_PASSWORD=cluster.password,
        MYSQL_TEST_SCHEMA="cppchat",
    )
    process = subprocess.Popen(
        [str(binary)],
        cwd=cluster.directory / "GateServer",
        env=environment,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    cluster.stack.callback(cluster.stop, process)
    cluster.register_process("mysql_pool_" + variant, process)
    output = queue.Queue()

    def consume():
        for line in process.stdout:
            output.put(line)
        output.put(None)

    reader = threading.Thread(target=consume, daemon=True)
    reader.start()
    locked = False
    lines = []
    try:
        while True:
            line = output.get(timeout=180)
            if line is None:
                break
            lines.append(line)
            if not line.startswith("CONTROL:"):
                continue
            command = line.strip().split(":", 1)[1]
            with cluster.sql(root=True) as db:
                with db.cursor() as cursor:
                    if command == "idle":
                        # Observe the pool's session with wait_timeout=1 specifically.
                        cursor.execute(
                            "SELECT ID FROM information_schema.PROCESSLIST WHERE USER=%s AND COMMAND='Sleep' ORDER BY ID DESC LIMIT 1",
                            (cluster.username,),
                        )
                        selected = cursor.fetchone()
                        require(selected, "Pool session missing")

                        def gone():
                            cursor.execute(
                                "SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE ID=%s",
                                (selected[0],),
                            )
                            return cursor.fetchone()[0] == 0

                        eventually(gone, 10, "MySQL idle session expired")
                    elif command == "block":
                        cursor.execute("ALTER USER %s@'%%' ACCOUNT LOCK", (cluster.username,))
                        locked = True
                        cursor.execute(
                            "SELECT ID FROM information_schema.PROCESSLIST WHERE USER=%s",
                            (cluster.username,),
                        )
                        for (connection_id,) in cursor.fetchall():
                            cursor.execute(f"KILL {int(connection_id)}")
                    elif command == "restore":
                        cursor.execute("ALTER USER %s@'%%' ACCOUNT UNLOCK", (cluster.username,))
                        locked = False
                    else:
                        raise AssertionError("Unexpected controller instruction")
            process.stdin.write("continue\n")
            process.stdin.flush()
        require(process.wait(timeout=10) == 0, "Recovery test failed: " + "".join(lines))
    finally:
        if locked:
            with cluster.sql(root=True) as db:
                with db.cursor() as cursor:
                    cursor.execute("ALTER USER %s@'%%' ACCOUNT UNLOCK", (cluster.username,))
        cluster.stop(process)
        reader.join(timeout=5)


@pytest.mark.feature("FAULT-MYSQL")
def test_database_fault_is_not_account_mismatch(recovery_cluster):
    cluster = recovery_cluster
    account = cluster.register()
    code = cluster.code(account["email"])
    request = {
        "user": account["user"],
        "email": account["email"],
        "passwd": uuid.uuid4().hex,
        "verifycode": code,
    }
    cluster.fault("mysql", False)
    try:
        reply = cluster.post("/reset_pwd", request)
        require(
            reply.get("error") == 1014, "Database fault misreported as account mismatch/success"
        )
    finally:
        cluster.fault("mysql", True)
    require(
        cluster.post("/reset_pwd", request).get("error") == 0, "Authentication failed to recover"
    )


@pytest.mark.feature("FAULT-REDIS")
def test_redis_disconnect_never_reports_success(recovery_cluster):
    cluster = recovery_cluster
    account = cluster.register()
    cluster.login(account)
    peer = cluster.resource(account)
    cluster.fault("redis", False)
    try:
        response = peer.request(
            1054,
            {
                "uid": account["uid"],
                "token": account["token"],
                "file_id": uuid.uuid4().hex,
                "offset": 0,
            },
        )
        require(not response.get("ok"), "Redis fault reported successful authorization")
    finally:
        cluster.fault("redis", True)
    require(cluster.redis.ping(), "Redis fixture failed to recover")
    import hashlib

    common = {"uid": account["uid"], "token": account["token"], "file_id": uuid.uuid4().hex}
    restored = peer.request(
        1050,
        dict(common, recipient=account["uid"] + 1, size=1, sha256=hashlib.sha256(b"x").hexdigest()),
    )
    require(restored.get("ok"), "Resource authorization did not recover after Redis reconnect")
