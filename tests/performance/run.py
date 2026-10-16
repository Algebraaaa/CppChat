#!/usr/bin/env python3
import argparse
import asyncio
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import psutil
from testcontainers.core.container import DockerContainer
from harness.checks import require
from harness.cluster import Cluster, executable
from harness.protocol import async_receive, encode, CHUNK_SIZE
from performance.resource_load import transfer
from performance.reporting import EXIT_CODES, measurement_report, validate_baseline, write_report


def percentile(values, fraction):
    require(bool(values), "No latency measurements")
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


async def measure(cluster, clients, duration, rate, on_ready=None, resource_every=0):
    require(clients >= 2 and clients % 2 == 0 and duration > 0 and rate > 0, "Invalid load profile")
    accounts = [cluster.register() for _ in range(clients)]
    threads = {}
    for index in range(0, clients, 2):
        a = cluster.login(accounts[index], "chat1")
        b = cluster.login(accounts[index + 1], "chat2")
        thread = cluster.friends(accounts[index], accounts[index + 1], a, b)
        threads[index] = threads[index + 1] = thread
    sent_at, acknowledgements, deliveries, snapshots, errors = {}, {}, {}, [], []
    writers, receivers = [], []
    resource_tasks = []
    prefix = "perf-" + uuid.uuid4().hex[:12]

    async def receiver(reader):
        try:
            while True:
                message_id, body = await async_receive(reader)
                if message_id in (1018, 1019):
                    require(body.get("error", 0) == 0, "Load message rejected")
                    for message in body.get("chat_datas", []):
                        key = message.get("unique_id", "")
                        if key in sent_at:
                            (acknowledgements if message_id == 1018 else deliveries).setdefault(
                                key, (time.perf_counter() - sent_at[key]) * 1000
                            )
        except asyncio.CancelledError:
            raise
        except Exception as error:
            errors.append(str(error))

    try:
        for index, account in enumerate(accounts):
            login = cluster.post(
                "/user_login", {"email": account["email"], "passwd": account["passwd"]}
            )
            require(login.get("error") == 0, "Load HTTP login failed")
            reader, writer = await asyncio.open_connection(
                "127.0.0.1", cluster.ports["chat1" if index % 2 == 0 else "chat2"]
            )
            writer.write(encode(1005, {"uid": account["uid"], "token": login["token"]}))
            await writer.drain()
            message_id, reply = await asyncio.wait_for(async_receive(reader), 10)
            require(message_id == 1006 and reply.get("error") == 0, "Load TCP login failed")
            account["token"] = reply["token"]
            cluster.credentials.append(reply["token"])
            writers.append(writer)
            receivers.append(asyncio.create_task(receiver(reader)))
        if on_ready:
            on_ready()
        started = time.perf_counter()
        rounds = math.ceil(duration * rate)
        for sequence in range(rounds):
            deadline = started + sequence / rate
            if time.perf_counter() < deadline:
                await asyncio.sleep(deadline - time.perf_counter())
            require(not errors, "Receiver error: " + "; ".join(errors))
            for index, writer in enumerate(writers):
                first, second = accounts[index], accounts[index ^ 1]
                key = f"{prefix}-{index}-{sequence}"
                body = {
                    "fromuid": first["uid"],
                    "touid": second["uid"],
                    "thread_id": threads[index],
                    "text_array": [{"unique_id": key, "content": "load 中文 😃", "msg_type": 0}],
                }
                sent_at[key] = time.perf_counter()
                writer.write(encode(1017, body))
                if sequence % max(1, round(10 * rate)) == 0:
                    writer.write(encode(1023, {"request_id": uuid.uuid4().hex}))
            await asyncio.gather(*(writer.drain() for writer in writers))
            if resource_every and sequence % (resource_every * rate) == 0:
                for index in range(0, clients, 2):
                    resource_tasks.append(
                        asyncio.create_task(
                            transfer(
                                cluster.ports["resource"],
                                accounts[index],
                                accounts[index + 1],
                                b"x" * CHUNK_SIZE,
                            )
                        )
                    )
            if sequence % max(1, round(rate)) == 0:
                sample = {
                    "seconds": time.perf_counter() - started,
                    "in_flight": len(sent_at) - len(acknowledgements),
                    "services": {},
                }
                for name, process in cluster.processes.items():
                    item = psutil.Process(process.pid)
                    sample["services"][name] = {
                        "rss": item.memory_info().rss,
                        "threads": item.num_threads(),
                        "handles": item.num_handles() if os.name == "nt" else item.num_fds(),
                    }
                snapshots.append(sample)

        # Await every ack AND recipient notification; a delay alone cannot pass.
        async def drained():
            while len(acknowledgements) < len(sent_at) or len(deliveries) < len(sent_at):
                require(not errors, "Receiver failed: " + "; ".join(errors))
                await asyncio.sleep(0.01)

        await asyncio.wait_for(drained(), timeout=30)
        elapsed = max(duration, time.perf_counter() - started)
        with cluster.sql() as db:
            with db.cursor() as cursor:
                cursor.execute(
                    "SELECT COUNT(*),COUNT(DISTINCT unique_id) FROM chat_message WHERE unique_id LIKE %s",
                    (prefix + "%",),
                )
                total, distinct = cursor.fetchone()
                require(
                    total == distinct == len(sent_at), "Load lost or duplicated persisted messages"
                )
        resource_times = await asyncio.gather(*resource_tasks)
        return {
            "connections": clients,
            "duration_seconds": duration,
            "rate_per_connection": rate,
            "messages": len(sent_at),
            "throughput": len(sent_at) / elapsed,
            "ack_p95_ms": percentile(list(acknowledgements.values()), 0.95),
            "ack_p99_ms": percentile(list(acknowledgements.values()), 0.99),
            "delivery_p95_ms": percentile(list(deliveries.values()), 0.95),
            "delivery_p99_ms": percentile(list(deliveries.values()), 0.99),
            "samples": snapshots,
            "receiver_errors": errors,
            "resource_transfers": len(resource_times),
            "resource_p95_ms": percentile(resource_times, 0.95) if resource_times else None,
            "resource_p99_ms": percentile(resource_times, 0.99) if resource_times else None,
        }
    finally:
        for task in receivers:
            task.cancel()
        for task in resource_tasks:
            if not task.done():
                task.cancel()
        await asyncio.gather(*resource_tasks, return_exceptions=True)
        await asyncio.gather(*receivers, return_exceptions=True)
        for writer in writers:
            writer.close()
        await asyncio.gather(*(writer.wait_closed() for writer in writers), return_exceptions=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", choices=["baseline", "soak"], default="baseline")
    parser.add_argument("--server-build", type=Path, default=Path("build/server"))
    parser.add_argument("--configuration", default="Debug")
    parser.add_argument("--artifacts", type=Path, default=Path("build/performance"))
    parser.add_argument("--approved-baseline", type=Path)
    parser.add_argument(
        "--mode",
        choices=["collect", "compare"],
        default="compare",
        help="Collect candidate measurements or compare against an explicitly approved baseline",
    )
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument(
        "--resource-every",
        type=int,
        default=0,
        help="Transfer a 32 KiB resource per user pair every N seconds",
    )
    parser.add_argument(
        "--connections", type=int, help="Short diagnostic override, not an approved baseline"
    )
    parser.add_argument(
        "--duration", type=int, help="Short diagnostic override, not an approved baseline"
    )
    args = parser.parse_args()
    args.artifacts.mkdir(parents=True, exist_ok=True)
    clients = args.connections or (100 if args.profile == "baseline" else 50)
    duration = args.duration or (600 if args.profile == "baseline" else 7200)
    require(args.runs > 0, "No measurement runs")
    require(args.resource_every >= 0, "Invalid resource interval")
    if args.mode == "collect":
        require(args.approved_baseline is None, "Collect mode cannot accept an approved baseline")
    elif args.approved_baseline is None:
        raise FileNotFoundError(
            "Compare mode requires --approved-baseline; use --mode collect to collect a candidate"
        )
    else:
        require(
            not args.connections and not args.duration,
            "Diagnostic overrides cannot serve as approved baselines",
        )
        require(args.runs == 3, "Performance comparison requires three measurements")
    binary = executable(args.server_build.resolve(), "ChatServer", args.configuration)
    environment = {
        "os": platform.platform(),
        "cpu": platform.processor(),
        "cores": psutil.cpu_count(),
        "configuration": args.configuration,
        "profile": args.profile,
        "connections": clients,
        "duration": duration,
        "resource_every": args.resource_every,
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
    }
    if not environment["cpu"] and Path("/proc/cpuinfo").is_file():
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                environment["cpu"] = line.split(":", 1)[1].strip()
                break
    measurements = []
    baseline = None
    if args.approved_baseline:
        baseline = json.loads(args.approved_baseline.read_text(encoding="utf-8"))
        validate_baseline(baseline, environment)
    for index in range(args.runs):
        run = args.artifacts / (
            datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            + f"-{index}-{uuid.uuid4().hex[:8]}"
        )
        print(f"Load run {index + 1}/{args.runs}: {clients} connections, {duration}s", flush=True)
        with Cluster(run, args.server_build, args.configuration) as cluster:
            # k6 runs its HTTP load against the same isolated Gate as TCP load.
            k6 = DockerContainer("grafana/k6:0.57.0")
            cluster.stack.callback(k6.stop)
            script = Path(__file__).with_name("http.js").resolve()
            k6.with_volume_mapping(str(script), "/tests/http.js", mode="ro")
            target = "host.docker.internal" if os.name == "nt" else "172.17.0.1"
            # Native Linux Gate listens loopback; use host networking there.
            if os.name != "nt":
                k6.with_kwargs(network_mode="host")
                target = "127.0.0.1"
            k6.with_env("GATE_URL", f"http://{target}:{cluster.ports['gate']}")
            k6.with_env("DURATION", f"{duration}s").with_command("run /tests/http.js")
            result = asyncio.run(
                measure(
                    cluster,
                    clients,
                    duration,
                    1,
                    on_ready=k6.start,
                    resource_every=args.resource_every,
                )
            )
            exit_status = k6.get_wrapped_container().wait(timeout=duration + 30)["StatusCode"]
            (run / "k6.log").write_bytes(b"".join(k6.get_logs()))
            require(exit_status == 0, "HTTP load failed thresholds")
            (run / "measurement.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
            measurements.append(result)
    report = measurement_report(environment, measurements, baseline)
    write_report(report, args.artifacts / "performance.json")
    print(f"{report['status']}: {report['reason']}", flush=True)
    return EXIT_CODES[report["status"]]


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        # Keep failure evidence even if setup or data-integrity checks abort a run.
        output = (
            Path(sys.argv[sys.argv.index("--artifacts") + 1])
            if "--artifacts" in sys.argv
            else Path("build/performance")
        )
        output.mkdir(parents=True, exist_ok=True)
        state = "BLOCKED" if isinstance(error, FileNotFoundError) else "FAIL"
        (output / "performance.json").write_text(
            json.dumps({"status": state, "reason": str(error)}, indent=2), encoding="utf-8"
        )
        print(f"{state}: {type(error).__name__}: {error}", flush=True)
        sys.exit(2 if state == "BLOCKED" else 1)
