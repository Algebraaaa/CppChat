"""Build pinned OpenSSL 3.5.9 for Qt/Android ARM64 on Linux or Windows/MSYS2.

Uses OpenSSL's Android build and KDAB's Qt library naming/export adaptation.
The Windows linker receives upstream arguments through response files, avoiding
cmd.exe's command length limit. Sources, objects and logs belong under build/.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile

SHA256 = "603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("archive", "workdir", "ndk", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--msys", type=Path, help="Required only on Windows")
    args = parser.parse_args()
    if os.name == "nt" and args.msys is None:
        parser.error("--msys is required on Windows")
    with args.archive.open("rb") as source:
        if hashlib.file_digest(source, "sha256").hexdigest() != SHA256:
            raise SystemExit("OpenSSL archive does not match publisher SHA256")
    args.workdir.mkdir(parents=True, exist_ok=True)
    source = args.workdir.resolve() / "openssl-3.5.9"
    if not source.exists():
        with tarfile.open(args.archive, "r:gz") as archive:
            archive.extractall(args.workdir, filter="data")
    config = source / "Configurations/15-android.conf"
    text = config.read_text(encoding="utf-8")
    marker = '        shared_extension => ".so",'
    if 'shlib_variant    => "_3"' not in text:
        if text.count(marker) != 1:
            raise SystemExit("Unexpected OpenSSL Android configuration")
        config.write_text(text.replace(marker, marker + '\n        shlib_variant    => "_3",'),
                          encoding="utf-8", newline="\n")
    exports = source / "util/mkdef.pl"
    text = exports.read_text(encoding="utf-8")
    exports.write_text(text.replace("OPENSSL${SO_VARIANT}_$thisversion", "OPENSSL_$thisversion"),
                       encoding="utf-8", newline="\n")
    ndk = args.ndk.resolve().as_posix()
    if os.name == "nt":
        bash = args.msys / "usr/bin/bash.exe"
        make = args.msys / "ucrt64/bin/mingw32-make.exe"
        shell = args.msys / "usr/bin/sh.exe"
        prefix = ("set -e; export PATH=/usr/bin:/ucrt64/bin:$PATH; "
                  f"export ANDROID_NDK_ROOT=$(cygpath -u {shlex.quote(ndk)}); "
                  'export PATH="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/windows-x86_64/bin:/usr/bin:/ucrt64/bin:$PATH"; '
                  f"cd {shlex.quote(source.as_posix())}; ")
        make_command = f"{shlex.quote(make.as_posix())} SHELL={shlex.quote(shell.as_posix())}"
    else:
        bash = shutil.which("bash")
        if not bash or not shutil.which("make"):
            raise SystemExit("Linux build requires bash, make and perl")
        prefix = (f"set -e; export ANDROID_NDK_ROOT={shlex.quote(ndk)}; "
                  'export PATH="$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"; '
                  f"cd {shlex.quote(source.as_posix())}; ")
        make_command = "make"

    def run(command, **kwargs):
        return subprocess.run([str(bash), "--noprofile", "--norc", "-c", prefix + command],
                              text=True, encoding="utf-8", errors="replace", **kwargs)

    run("perl Configure android-arm64 -D__ANDROID_API__=28 shared no-tests no-apps no-docs "
        "no-module no-legacy no-comp no-asm '-Wl,-z,max-page-size=16384'", check=True)
    result = run(make_command + " -j6 build_libs", capture_output=True)
    (args.workdir / "openssl-build.log").write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        if os.name != "nt" or "command line is too long" not in result.stdout + result.stderr:
            raise SystemExit("OpenSSL compilation failed; inspect openssl-build.log")
        dry = run(make_command + " -n libcrypto_3.so libssl_3.so", capture_output=True, check=True)
        logical = dry.stdout.replace("\\\n", " ")
        commands = [shlex.split(line) for line in logical.splitlines()
                    if line.startswith("aarch64-linux-android28-clang ")]
        for command in commands:
            output = command[command.index("-o") + 1]
            if output not in ("libcrypto_3.so", "libssl_3.so"):
                raise SystemExit("Unexpected OpenSSL link output")
            # Bypass the Unix symlink that make -n has intentionally not created.
            command = ["-l:libcrypto_3.so" if x == "-lcrypto" else x for x in command]
            response = source / (output + ".rsp")
            response.write_text(subprocess.list2cmdline(command[1:]), encoding="utf-8")
            compiler = args.ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android28-clang.cmd"
            subprocess.run([str(compiler), "@" + str(response)], cwd=source, check=True)
    args.output.mkdir(parents=True, exist_ok=True)
    for name in ("libcrypto_3.so", "libssl_3.so"):
        shutil.copy2(source / name, args.output / name)
    shutil.copy2(source / "LICENSE.txt", args.output / "OpenSSL-LICENSE.txt")
    print("OpenSSL 3.5.9 Android ARM64 libraries ready:", args.output)


if __name__ == "__main__":
    main()
