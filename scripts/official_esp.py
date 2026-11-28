"""Opt-in official ESP-IDF release packages, separate from the reference lock."""

import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tarfile
import zipfile

from build_support import file_sha256
from esp_tool_integrity import tool_tree_digest

PIN = Path(__file__).resolve().parent / "requirements/native-official-esp-linux-x86_64.json"


def pins():
    return json.loads(PIN.read_text())


def verify(core):
    lock = pins()
    actual = {}
    for name, item in lock["packages"].items():
        path = core / "packages" / name
        for entry in path.rglob("*"):
            if entry.is_symlink() and (
                Path(os.readlink(entry)).is_absolute() or not entry.resolve().is_relative_to(path.resolve())
            ):
                raise RuntimeError(f"External link in official SDK package: {entry}")
            if entry.name == ".git":
                raise RuntimeError("Unexpected Git metadata in official SDK package")
        actual[name] = tool_tree_digest(path)
        if actual[name] != item["tree_sha256"]:
            raise RuntimeError(f"Official SDK package differs from lock: {name}")
    return {"release": lock["release"], "commit": lock["commit"], "tools": actual}


def extract_zip(archive, directory):
    with zipfile.ZipFile(archive) as source:
        for name in source.namelist():
            relative = PurePosixPath(name)
            if relative.is_absolute() or ".." in relative.parts:
                raise RuntimeError("Unsafe official ZIP member")
    subprocess.run(["unzip", "-q", str(archive), "-d", str(directory)], check=True)


def install(core, archives, installer, legacy_lock, content_addressed):
    lock = pins()
    # Retain the already qualified exact GCC package, independently verified
    # against both profiles' identical tree pin.
    installer.install_packages(
        core,
        archives,
        legacy_lock,
        locations={"packages/toolchain-riscv32-esp"},
        content_addressed=content_addressed,
    )
    for name, item in lock["packages"].items():
        if name == "toolchain-riscv32-esp":
            continue
        cache = archives / item["sha256"] if content_addressed else archives
        archive = cache / item["filename"]
        installer.fetch(item["url"], archive, item["sha256"])
        if archive.stat().st_size != item["size"] or file_sha256(archive) != item["sha256"]:
            raise RuntimeError(f"Official package archive differs: {name}")
        staging = core / ("extract-" + name)
        staging.mkdir()
        if item["filename"].endswith(".zip"):
            extract_zip(archive, staging)
        else:
            with tarfile.open(archive) as source:
                source.extractall(staging, filter="data")
        source = staging / item["directory"] if item["directory"] else staging
        if name == "framework-espidf":
            # The vendor release ZIP includes Git metadata for SDK/submodules.
            # Never execute its hooks or depend on mutable Git index contents.
            for entry in sorted(source.rglob(".git"), key=lambda p: len(p.parts), reverse=True):
                if entry.is_dir() and not entry.is_symlink():
                    shutil.rmtree(entry)
                else:
                    entry.unlink()
            with (source / "version.txt").open("xb") as stream:
                stream.write(lock["release"].encode())
        source.rename(core / "packages" / name)
        if staging.exists():
            staging.rmdir()
    return verify(core)
