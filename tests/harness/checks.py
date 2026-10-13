import time
from threading import Event


def require(condition, message="Unexpected result"):
    """Deliberately not a Python assert: checks also run under python -O."""
    if not condition:
        raise AssertionError(message)


def eventually(predicate, timeout=30, description="condition", retry_on=(OSError, ConnectionError)):
    deadline = time.monotonic() + timeout
    wake = Event()
    last = None
    while time.monotonic() < deadline:
        try:
            value = predicate()
            if value:
                return value
        except retry_on as error:
            last = error
        # Poll a condition with a deadline; elapsed time never counts as success.
        wake.wait(min(0.05, max(0, deadline - time.monotonic())))
    raise TimeoutError(f"Timed out waiting for {description}: {last}")
