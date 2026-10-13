import shutil
import pytest
from harness.checks import require
from harness.cluster import Cluster


@pytest.mark.integration
@pytest.mark.feature("TEST-CLEANUP")
def test_partial_setup_failure_reclaims_created_resources(tmp_path, docker_available):
    class FailingCluster(Cluster):
        def start(self):
            self.directory.mkdir(parents=True)
            self.owned_container = self.container("redis", [6379])
            self.container_id = self.owned_container.get_wrapped_container().id
            self.process = self.spawn(
                "VerifyServer", [shutil.which("node") or "node", "-e", "setInterval(()=>{},1000)"]
            )
            raise RuntimeError("injected setup failure")

    cluster = FailingCluster(tmp_path / "partial", tmp_path)
    with pytest.raises(RuntimeError, match="injected setup failure"):
        with cluster:
            pass
    require(cluster.process.poll() is not None, "Partial setup leaked a native process")
    import docker

    client = docker.from_env()
    try:
        require(
            not client.containers.list(all=True, filters={"id": cluster.container_id}),
            "Partial setup leaked a container",
        )
    finally:
        client.close()
