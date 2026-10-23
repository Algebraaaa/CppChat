"""Inspect the actual APK's ABI, ELF alignment, TLS runtime and private-data exclusions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import zipfile


def audit(apk, forbidden):
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        required = ("AndroidManifest.xml", "classes.dex", "lib/arm64-v8a/libssl_3.so",
                    "lib/arm64-v8a/libcrypto_3.so", "assets/licenses/OpenSSL-3.5.9.txt")
        for name in required:
            if name not in names:
                raise ValueError("Missing packaged dependency: " + name)
        libraries = [name for name in names if name.startswith("lib/") and name.endswith(".so")]
        for name in libraries:
            if not name.startswith("lib/arm64-v8a/"):
                raise ValueError("Unexpected ABI: " + name)
            data = archive.read(name)
            if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 183:
                raise ValueError("Not little-endian AArch64 ELF64: " + name)
            offset = struct.unpack_from("<Q", data, 32)[0]
            stride, count = struct.unpack_from("<HH", data, 54)
            for i in range(count):
                header = offset + i * stride
                if struct.unpack_from("<I", data, header)[0] == 1:
                    if struct.unpack_from("<Q", data, header + 48)[0] < 16384:
                        raise ValueError("ELF LOAD alignment below 16 KiB: " + name)
        crypto = archive.read("lib/arm64-v8a/libcrypto_3.so")
        ssl = archive.read("lib/arm64-v8a/libssl_3.so")
        if b"OpenSSL 3.5.9" not in crypto or b"OPENSSL_3.0.0" not in crypto:
            raise ValueError("OpenSSL version/export compatibility mismatch")
        if b"libcrypto_3.so" not in ssl:
            raise ValueError("TLS library does not reference the packaged crypto library")
        for name in names:
            if name.endswith((".jks", ".keystore", ".sqlite", ".db", ".log")):
                raise ValueError("Private/build data in APK: " + name)
            if forbidden and not name.endswith("/"):
                data = archive.read(name)
                if any(value in data for value in forbidden):
                    raise ValueError("Forbidden test credential found in: " + name)
    with apk.open("rb") as source:
        digest = hashlib.file_digest(source, "sha256").hexdigest()
    return {"apk": apk.name, "bytes": apk.stat().st_size, "sha256": digest,
            "abi": "arm64-v8a", "elf_libraries_checked": len(libraries),
            "elf_load_alignment": ">=16384", "openssl": "3.5.9",
            "credential_scan": "PASS" if forbidden else "NOT_RUN", "status": "PASS"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--forbid-env", action="append", default=[])
    parser.add_argument("--badging", type=Path)
    parser.add_argument("--media", action="store_true", help="Require camera/microphone and multimedia runtime")
    args = parser.parse_args()
    values = [os.environ[name].encode() for name in args.forbid_env if os.environ.get(name)]
    result = audit(args.apk, values)
    if args.badging:
        manifest = args.badging.read_text(encoding="utf-8-sig")
        for permission in ("WRITE_EXTERNAL_STORAGE", "READ_EXTERNAL_STORAGE", "MANAGE_EXTERNAL_STORAGE"):
            if "uses-permission: name='android.permission." + permission + "'" in manifest:
                raise ValueError("Unneeded packaged permission: " + permission)
        result["broad_storage_permissions"] = "ABSENT"
        if args.media:
            for permission in ("RECORD_AUDIO", "CAMERA"):
                if "uses-permission: name='android.permission." + permission + "'" not in manifest:
                    raise ValueError("Required media permission missing: " + permission)
            with zipfile.ZipFile(args.apk) as archive:
                if not any("Qt6Multimedia" in name for name in archive.namelist()):
                    raise ValueError("Qt Multimedia runtime missing")
                if "assets/licenses/Opus-1.5.2.txt" not in archive.namelist():
                    raise ValueError("Opus license missing")
            result["media_permissions_and_runtime"] = "PASS"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
