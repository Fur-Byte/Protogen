#!/usr/bin/env python3
"""Provision a fresh Linux x86-64 ESP SDK from pinned artifacts without package hooks."""

import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import urllib.error
from urllib.parse import urlsplit

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
            request = urllib.request.Request(url)
            # The registry download endpoint negotiates downloads for the
            # pinned PlatformIO client. Other hosts retain urllib's default.
            if urlsplit(url).hostname == "dl.registry.platformio.org":
                version = json.loads((ROOT / "toolchain.lock.json").read_text())["esp32"]["platformio"]
                request.add_header("User-Agent", f"PlatformIO/{version}")
                request.add_header("Accept", "application/octet-stream")
            try:
                with urllib.request.urlopen(request, timeout=120) as response:
                    shutil.copyfileobj(response, stream)
            except urllib.error.URLError as error:
                raise RuntimeError(
                    f"Cannot download pinned artifact {destination.name} from {urlsplit(url).hostname}: {error.reason}"
                ) from error
            stream.flush()
            if sha(temporary) != digest:
                raise RuntimeError(f"Downloaded artifact differs from pin: {destination.name}")
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)


def parse_locked_requirement(line):
    parts = line.split()
    if not parts or not re.fullmatch(r"[A-Za-z0-9_.-]+==[A-Za-z0-9_.+!-]+", parts[0]):
        raise RuntimeError("Expected one exact pinned requirement")
    if len(parts) < 2 or any(not re.fullmatch(r"--hash=sha256:[0-9a-f]{64}", x) for x in parts[1:]):
        raise RuntimeError("Expected SHA-256 artifact pins")
    name, version = parts[0].split("==")
    return name, version, {x.split(":")[1] for x in parts[1:]}


def download_python(wheelhouse, groups=("platformio", "idf")):
    # Fetch only immutable, non-yanked artifacts explicitly authorized by the
    # lock. Pip selects the compatible wheel offline; do not execute arbitrary
    # source distributions during discovery.
    for group in groups:
        for line in (LOCKS / (group + SUFFIX)).read_text().splitlines():
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            name, version, digests = parse_locked_requirement(line)
            with urllib.request.urlopen(
                f"https://pypi.org/pypi/{name}/{version}/json", timeout=30
            ) as response:
                release = json.load(response)
            matches = [
                item
                for item in release["urls"]
                if item["digests"]["sha256"] in digests
                and not item["yanked"]
                and (
                    item["filename"].endswith(".whl")
                    or (group == "idf" and name == "esptool" and item["filename"].endswith(".tar.gz"))
                )
            ]
            if not matches:
                raise RuntimeError(f"Missing pinned PyPI release artifact: {name}=={version}")
            for artifact in matches:
                if Path(artifact["filename"]).name != artifact["filename"]:
                    raise RuntimeError("Invalid artifact filename")
                fetch(
                    artifact["url"], wheelhouse / group / artifact["filename"], artifact["digests"]["sha256"]
                )


def install_packages(core, archives, lock, *, locations=None, content_addressed=False):
    manifest = json.loads((LOCKS / "esp-packages-linux-x86_64.json").read_text())
    tools = json.loads((LOCKS / "esp-tools-linux-x86_64.json").read_text())
    for location, item in manifest.items():
        if locations is not None and location not in locations:
            continue
        name = Path(location).name
        pin = tools[name] if name in tools else lock["esp32_source_integrity"][location]
        if item["sha256"] != pin["archive_sha256"]:
            raise RuntimeError(f"Conflicting package pins: {name}")
        cache = archives / item["sha256"] if content_addressed else archives
        archive = cache / Path(item["url"]).name
        fetch(item["url"], archive, item["sha256"])
        target = core / location
        target.mkdir(parents=True)
        with tarfile.open(archive) as source:
            source.extractall(target, filter="data")
        (target / ".piopm").write_text(json.dumps(item["metadata"]))
    if locations is None:
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
    verify_esp_sources(core, lock)
    verify_esp_tools(core)
    print("Pinned ESP packages and Python environments installed without upstream postinstall hooks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
