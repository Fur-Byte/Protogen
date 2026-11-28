#!/usr/bin/env python3
"""Create isolated, hash-locked ESP development Python environments offline."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parents[1]
SUFFIX = "-linux-x86_64-py313.txt"


def pip_install(python, wheelhouse, lock, *flags):
    subprocess.run(
        [
            str(python),
            "-m",
            "pip",
            "--isolated",
            "install",
            "--no-index",
            "--no-cache-dir",
            "--find-links",
            str(wheelhouse),
            "--require-hashes",
            "--no-build-isolation",
            *flags,
            "-r",
            str(lock),
        ],
        check=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wheelhouse", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--native-only", action="store_true", help="Install only the native IDF environment")
    args = parser.parse_args()
    if (platform.system(), platform.machine(), sys.implementation.name, sys.version_info[:2]) != (
        "Linux",
        "x86_64",
        "cpython",
        (3, 13),
    ):
        parser.error("Only Linux x86-64 CPython 3.13 is currently qualified")
    source = args.wheelhouse.resolve()
    output = args.output.resolve()
    groups = ("idf",) if args.native_only else ("platformio", "idf")
    if any(not (source / group).is_dir() for group in groups):
        parser.error("Wheelhouse must contain the requested Python group directories")
    if args.output.exists() or args.output.is_symlink():
        parser.error("Output must be a new directory; existing environments are never modified")
    output.mkdir(parents=True)
    locks = ROOT / "scripts/requirements"
    for group in ("idf",) if args.native_only else ("platformio", "idf"):
        target = output / group
        venv.EnvBuilder(with_pip=True).create(target)
        python = target / "bin/python"
        if group == "idf":
            # esptool has only an sdist; its declared build backend is pinned
            # and installed before the offline, non-isolated source build.
            pip_install(python, source / group, locks / ("idf-build" + SUFFIX), "--only-binary=:all:")
        flags = ("--only-binary=:all:",) if group == "platformio" else ()
        pip_install(python, source / group, locks / (group + SUFFIX), *flags)
        subprocess.run([str(python), "-m", "pip", "check"], check=True)
        subprocess.run(
            [str(python), str(ROOT / "scripts/check-esp-python-compatibility.py"), group], check=True
        )
    report = {
        "state": "success",
        "python": platform.python_version(),
        "groups": list(groups),
        "locks": {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(locks.glob("*" + SUFFIX))
            if path.name == "idf-build" + SUFFIX or any(path.name == group + SUFFIX for group in groups)
        },
    }
    (output / "verified-install.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
