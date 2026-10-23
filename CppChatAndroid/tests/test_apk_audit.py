"""Package-policy fixtures only; these archives are not runnable Android APKs."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "apk_audit", Path(__file__).with_name("audit_android_apk.py")
)
apk_audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(apk_audit)


def arm64_library(payload=b""):
    header = struct.pack(
        "<16sHHIQQQIHHHHHH",
        b"\x7fELF\x02\x01\x01" + b"\0" * 9,
        3,
        183,
        1,
        0,
        64,
        0,
        0,
        64,
        56,
        1,
        0,
        0,
        0,
    )
    segment = struct.pack(
        "<IIQQQQQQ", 1, 5, 0, 0, 0, 120 + len(payload), 120 + len(payload), 16384
    )
    return header + segment + payload


class ApkPolicyTests(unittest.TestCase):
    def setUp(self):
        root = ROOT / "build/packaging/apk-audit-tests"
        root.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=root)
        self.addCleanup(self.temporary.cleanup)
        self.apk = Path(self.temporary.name) / "format-fixture.apk"
        self.entries = {
            "AndroidManifest.xml": b"synthetic manifest; not binary XML",
            "classes.dex": b"synthetic dex placeholder",
            "assets/licenses/OpenSSL-3.5.9.txt": b"fixture license",
            "lib/arm64-v8a/libcrypto_3.so": arm64_library(
                b"OpenSSL 3.5.9 OPENSSL_3.0.0"
            ),
            "lib/arm64-v8a/libssl_3.so": arm64_library(b"libcrypto_3.so"),
            "lib/arm64-v8a/libQt6Quick_arm64-v8a.so": arm64_library(),
        }

    def inspect(self):
        with zipfile.ZipFile(self.apk, "w") as archive:
            for name, data in self.entries.items():
                archive.writestr(name, data)
        return apk_audit.audit(self.apk, [])

    def test_runtime_package_passes_without_test_modules(self):
        self.entries["assets/licenses/Test-font.txt"] = (
            b"A normal asset may contain the word Test"
        )
        self.assertTrue(self.inspect()["test_runtime_excluded"])

    def test_transitively_deployed_test_libraries_are_rejected(self):
        for name in (
            "libQt6Test_arm64-v8a.so",
            "libQt6QuickTest_arm64-v8a.so",
            "libqml_QtTest_quicktestplugin_arm64-v8a.so",
        ):
            with self.subTest(name=name):
                self.entries["lib/arm64-v8a/" + name] = arm64_library()
                with self.assertRaisesRegex(ValueError, "Test runtime"):
                    self.inspect()
                del self.entries["lib/arm64-v8a/" + name]

    def test_packaged_private_signing_archives_are_rejected(self):
        for extension in ("p12", "pfx", "jks", "keystore"):
            with self.subTest(extension=extension):
                self.entries["assets/identity." + extension] = (
                    b"private material placeholder"
                )
                with self.assertRaisesRegex(ValueError, "Private/build data"):
                    self.inspect()
                del self.entries["assets/identity." + extension]


if __name__ == "__main__":
    unittest.main()
