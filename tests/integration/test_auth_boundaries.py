from concurrent.futures import ThreadPoolExecutor
import uuid
import pytest
from harness.checks import require

pytestmark = pytest.mark.integration


@pytest.mark.feature("AUTH-VALIDATION")
@pytest.mark.parametrize(
    "route,fields",
    [
        ("/user_register", ["user", "email", "passwd", "confirm", "verifycode"]),
        ("/reset_pwd", ["user", "email", "passwd", "verifycode"]),
        ("/user_login", ["email", "passwd"]),
    ],
)
@pytest.mark.parametrize("bad", [None, "", 123, [], {}])
def test_invalid_fields(cluster, route, fields, bad):
    for field in fields:
        request = {name: "valid" for name in fields}
        request[field] = bad
        require(cluster.post(route, request).get("error") != 0, f"Invalid {field} accepted")


@pytest.mark.feature("AUTH-OTP")
def test_wrong_expired_duplicate_and_single_use(cluster):
    account = cluster.register()
    code = cluster.code(account["email"])
    require(cluster.code(account["email"]) == code, "Repeated code request changed unexpired code")
    request = {
        "user": account["user"],
        "email": account["email"],
        "passwd": account["passwd"],
        "verifycode": "wrong",
    }
    require(cluster.post("/reset_pwd", request).get("error") == 1004, "Wrong code misclassified")
    cluster.redis.delete("code_" + account["email"])
    request["verifycode"] = code
    require(cluster.post("/reset_pwd", request).get("error") == 1003, "Expired code misclassified")
    request["verifycode"] = cluster.code(account["email"])
    duplicate = cluster.post("/user_register", dict(request, confirm=request["passwd"]))
    require(duplicate.get("error") != 0, "Duplicate account accepted")


@pytest.mark.nightly
@pytest.mark.feature("AUTH-CONCURRENT-RESET")
def test_concurrent_reset_consumes_code_once(cluster):
    account = cluster.register()
    code = cluster.code(account["email"])
    request = {"user": account["user"], "email": account["email"], "verifycode": code}
    with ThreadPoolExecutor(max_workers=8) as workers:
        results = list(
            workers.map(
                lambda _: cluster.post("/reset_pwd", dict(request, passwd=uuid.uuid4().hex)),
                range(8),
            )
        )
    require(
        sum(reply.get("error") == 0 for reply in results) == 1,
        "Concurrent resets reused one-time verification code",
    )
