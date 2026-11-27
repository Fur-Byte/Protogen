#!/usr/bin/env python3
"""Provision a fresh Linux x86-64 ESP SDK from pinned artifacts without package hooks."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

from esp_source_integrity import verify_esp_sources
from esp_tool_integrity import verify_esp_tools

ROOT = Path(__file__).resolve().parents[1]
LOCKS = ROOT / "scripts/requirements"
SUFFIX = "-linux-x86_64-py313.txt"


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def fetch(url, destination, digest):
    if destination.is_file():
        if sha(destination) != digest:
            raise RuntimeError(f"Cached artifact differs from pin: {destination.name}")
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                shutil.copyfileobj(response, stream)
            stream.flush()
            if sha(temporary) != digest:
                raise RuntimeError(f"Downloaded artifact differs from pin: {destination.name}")
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)


def download_python(wheelhouse):
    for group in ("platformio", "idf", "esptool-contrib"):
        for line in (LOCKS / (group + SUFFIX)).read_text().splitlines():
            if not line or line.startswith("#"):
                continue
            requirement, digest = line.split(" --hash=sha256:")
            name, version = requirement.split("==")
            with urllib.request.urlopen(
                f"https://pypi.org/pypi/{name}/{version}/json", timeout=30
            ) as response:
                release = json.load(response)
            matches = [
                item for item in release["urls"] if item["digests"]["sha256"] == digest and not item["yanked"]
            ]
            if len(matches) != 1:
                raise RuntimeError(f"Missing pinned PyPI release artifact: {requirement}")
            artifact = matches[0]
            if Path(artifact["filename"]).name != artifact["filename"]:
                raise RuntimeError("Invalid artifact filename")
            fetch(artifact["url"], wheelhouse / group / artifact["filename"], digest)


def install_packages(core, archives, lock):
    manifest = json.loads((LOCKS / "esp-packages-linux-x86_64.json").read_text())
    tools = json.loads((LOCKS / "esp-tools-linux-x86_64.json").read_text())
    for location, item in manifest.items():
        name = Path(location).name
        pin = tools[name] if name in tools else lock["esp32_source_integrity"][location]
        if item["sha256"] != pin["archive_sha256"]:
            raise RuntimeError(f"Conflicting package pins: {name}")
        archive = archives / Path(item["url"]).name
        fetch(item["url"], archive, item["sha256"])
        target = core / location
        target.mkdir(parents=True)
        with tarfile.open(archive) as source:
            source.extractall(target, filter="data")
        (target / ".piopm").write_text(json.dumps(item["metadata"]))
    verify_esp_sources(core, lock)
    verify_esp_tools(core)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wheelhouse", type=Path)
    parser.add_argument("--archives", type=Path)
    args = parser.parse_args()
    if (platform.system(), platform.machine(), sys.implementation.name, sys.version_info[:2]) != (
        "Linux",
        "x86_64",
        "cpython",
        (3, 13),
    ):
        parser.error("Locked ESP provisioning is qualified only for Linux x86-64 CPython 3.13")
    build = ROOT / "build"
    core = build / "platformio"
    output = build / "locked-python"
    pio_env = build / "esp32-venv"
    for target in (core, output, pio_env):
        if target.exists() or target.is_symlink():
            parser.error(f"Use a fresh build directory; refusing to replace {target}")
    lock = json.loads((ROOT / "toolchain.lock.json").read_text())
    wheelhouse = (args.wheelhouse or build / "locked-wheelhouse").resolve()
    archives = (args.archives or build / "locked-archives").resolve()
    if args.wheelhouse is None:
        download_python(wheelhouse)
    install_packages(core, archives, lock)
    subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/install-locked-python.py"),
            "--wheelhouse",
            str(wheelhouse),
            "--output",
            str(output),
        ],
        check=True,
    )
    # Keep the venvs at their creation paths; their entrypoint shebangs use
    # absolute paths. Reproduce the reviewed upstream IDF metadata contract.
    pio_env.symlink_to(output / "platformio", target_is_directory=True)
    idf = output / "idf"
    sdk_env = core / "penv" / (".espidf-" + lock["esp32"]["esp_idf"])
    sdk_env.parent.mkdir()
    sdk_env.symlink_to(idf, target_is_directory=True)
    version = sys.version_info
    (idf / "pio-idf-venv.json").write_text(
        json.dumps(
            {
                "version": "1.0.0",
                "python_version": f"{version.major}.{version.minor}.{version.micro}-{version.releaselevel}.{version.serial}",
            }
        )
    )
    shutil.copytree(output / "esptool-contrib", core / "packages/tool-esptoolpy/_contrib")
    verify_esp_sources(core, lock)
    verify_esp_tools(core)
    print("Pinned ESP packages and Python environments installed without upstream postinstall hooks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
