import base64
import hashlib
import uuid
import pytest
from harness.checks import require
from harness.protocol import CHUNK_SIZE, IMAGE_LIMIT

pytestmark = [pytest.mark.integration, pytest.mark.feature("IMAGE-BOUNDS")]


@pytest.mark.parametrize("size", [0, 1, IMAGE_LIMIT - 1, IMAGE_LIMIT, IMAGE_LIMIT + 1])
def test_declared_size_boundary(cluster, pair, size):
    first, second, *_ = pair
    resource = cluster.resource(first)
    reply = resource.request(
        1050,
        {
            "uid": first["uid"],
            "token": first["token"],
            "file_id": uuid.uuid4().hex,
            "recipient": second["uid"],
            "size": size,
            "sha256": "a" * 64,
        },
    )
    require(bool(reply.get("ok")) == (0 < size <= IMAGE_LIMIT), "Image size boundary wrong")


@pytest.mark.parametrize("length", [CHUNK_SIZE - 1, CHUNK_SIZE, CHUNK_SIZE + 1])
def test_chunk_boundary(cluster, pair, length):
    first, second, *_ = pair
    data = b"x" * length
    file_id = uuid.uuid4().hex
    resource = cluster.resource(first)
    common = {"uid": first["uid"], "token": first["token"], "file_id": file_id}
    require(
        resource.request(
            1050,
            dict(
                common,
                recipient=second["uid"],
                size=length,
                sha256=hashlib.sha256(data).hexdigest(),
            ),
        ).get("ok")
    )
    response = resource.request(1052, dict(common, offset=0, data=base64.b64encode(data).decode()))
    require(bool(response.get("ok")) == (length <= CHUNK_SIZE), "Chunk size boundary wrong")


def test_resume_offset_sha_and_authorization(cluster, pair):
    first, second, *_ = pair
    data = b"x" * (CHUNK_SIZE + 1)
    file_id = uuid.uuid4().hex
    resource = cluster.resource(first)
    common = {"uid": first["uid"], "token": first["token"], "file_id": file_id}
    metadata = dict(
        common, recipient=second["uid"], size=len(data), sha256=hashlib.sha256(data).hexdigest()
    )
    require(resource.request(1050, metadata).get("ok"))
    require(
        not resource.request(1054, dict(common, offset=0)).get("ok"),
        "Incomplete resource downloadable",
    )
    require(
        not resource.request(
            1052, dict(common, offset=1, data=base64.b64encode(b"x").decode())
        ).get("ok"),
        "Wrong offset accepted",
    )
    require(
        resource.request(
            1052, dict(common, offset=0, data=base64.b64encode(data[:CHUNK_SIZE]).decode())
        ).get("ok")
    )
    resource.close()
    resource = cluster.resource(first)
    resumed = resource.request(1050, metadata)
    require(resumed.get("ok") and resumed["offset"] == CHUNK_SIZE, "Resume lost offset")
    require(
        resource.request(
            1052, dict(common, offset=CHUNK_SIZE, data=base64.b64encode(data[CHUNK_SIZE:]).decode())
        ).get("ok")
    )
    require(resource.request(1050, metadata).get("ok"), "Duplicate upload rejected")
    require(
        not resource.request(1050, dict(metadata, sha256="0" * 64)).get("ok"),
        "Conflicting upload accepted",
    )
    require(
        not resource.request(1054, dict(common, offset=len(data) + 1)).get("ok"),
        "Download offset overflow accepted",
    )
    stranger = cluster.register()
    cluster.login(stranger)
    unauthorised = cluster.resource(stranger).request(
        1054, dict(common, uid=stranger["uid"], token=stranger["token"], offset=0)
    )
    require(not unauthorised.get("ok"), "Third party downloaded image")
    require(not resource.request(1054, dict(common, token="invalid", offset=0)).get("ok"))
    require(not resource.request(1054, dict(common, file_id="../config.ini", offset=0)).get("ok"))
    cluster.redis.persist("resource_token_" + str(first["uid"]))
    require(
        not resource.request(1054, dict(common, offset=0)).get("ok"), "Non-expiring token accepted"
    )


def test_corrupt_image_never_ready(cluster, pair):
    first, second, *_ = pair
    resource = cluster.resource(first)
    file_id = uuid.uuid4().hex
    common = {"uid": first["uid"], "token": first["token"], "file_id": file_id}
    require(
        resource.request(1050, dict(common, recipient=second["uid"], size=1, sha256="0" * 64)).get(
            "ok"
        )
    )
    require(
        not resource.request(
            1052, dict(common, offset=0, data=base64.b64encode(b"x").decode())
        ).get("ok")
    )
    require(not cluster.redis.exists("resource_ready_" + file_id), "Bad checksum marked ready")
