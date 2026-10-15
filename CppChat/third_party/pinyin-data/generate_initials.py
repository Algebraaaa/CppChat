"""Derive only contact-index initials from the pinned upstream dictionary."""

import hashlib
from pathlib import Path
import re
import sys
import unicodedata

COMMIT = "9193766130af24d2ac54230be979b2e98ac66223"


def generate(source):
    result = {}
    for line in source.splitlines():
        match = re.match(r"U\+([0-9A-F]+):\s*([^,#]+)", line)
        if not match:
            continue
        initial = unicodedata.normalize("NFD", match[2].strip())[0].upper()
        if "A" <= initial <= "Z":
            result[int(match[1], 16)] = initial
    return "".join(
        f"{point:X} {initial}\n" for point, initial in sorted(result.items())
    )


if __name__ == "__main__":
    data = Path(sys.argv[1]).read_bytes()
    destination = Path(__file__).with_name("initials.txt")
    destination.write_text(
        generate(data.decode("utf-8")), encoding="ascii", newline="\n"
    )
    print(
        f"Pinned upstream: {COMMIT}; input SHA256: {hashlib.sha256(data).hexdigest()}"
    )
