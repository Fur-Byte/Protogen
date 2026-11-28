"""Verify Linux ESP build tool packages against reviewed upstream release trees."""

import json
import os
from pathlib import Path
import platform
import subprocess
import shutil

from build_support import file_sha256, source_digest

PIN_PATH = Path(__file__).resolve().parent / "requirements/esp-tools-linux-x86_64.json"
OPTIONAL = {"tool-riscv32-esp-elf-gdb", "tool-xtensa-esp-elf-gdb"}


def tool_tree_digest(directory):
    if not directory.is_dir() or directory.is_symlink():
        raise RuntimeError(f"Missing or symlinked ESP tool package: {directory}")
    files = {}
    for path in directory.rglob("*"):
        relative = path.relative_to(directory)
        name = relative.as_posix()
        if path.is_symlink():
            # Verify the exact upstream link text, not the dereferenced file.
            files[name] = "symlink:" + os.readlink(path)
        elif path.is_dir():
            continue
        elif not path.is_file():
            raise RuntimeError(f"Unexpected ESP tool entry: {path}")
        elif name != ".piopm" and not ("__pycache__" in relative.parts and path.suffix == ".pyc"):
            files[name] = file_sha256(path)
    return source_digest(files)


def verify_locked_scons_selection(pio, core, env):
    """Resolve the parent tool's actual SCons choice without allowing installation."""
    executable = shutil.which(str(pio), path=env.get("PATH"))
    if not executable:
        raise RuntimeError("Locked PlatformIO executable is unavailable")
    python = Path(executable).resolve().with_name("python")
    result = subprocess.run(
        [
            str(python),
            "-c",
            "import json; from platformio.package.manager.core import get_core_package_dir; "
            "print(json.dumps(get_core_package_dir('tool-scons', auto_install=False)))",
        ],
        env=dict(env, PLATFORMIO_CORE_DIR=str(Path(core).resolve())),
        capture_output=True,
        text=True,
        check=True,
    )
    selected = json.loads(result.stdout)
    if not selected or Path(selected).resolve() != (Path(core) / "packages/tool-scons").resolve():
        raise RuntimeError(
            "Locked SCons does not satisfy PlatformIO core; refusing runtime package installation"
        )
    return selected


def verify_esp_tools(core, pins=None):
    host = (platform.system(), platform.machine())
    if host != ("Linux", "x86_64"):
        return {"host": list(host), "state": "unverified_host"}
    if pins is None:
        pins = json.loads(PIN_PATH.read_text())
    verified, absent = {}, []
    for name, pin in sorted(pins.items()):
        directory = Path(core) / "packages" / name
        if name in OPTIONAL and not directory.exists() and not directory.is_symlink():
            absent.append(name)
            continue
        actual = tool_tree_digest(directory)
        if actual != pin["tree_sha256"]:
            raise RuntimeError(f"Pinned ESP tool content mismatch: {name}")
        verified[name] = actual
    return {
        "host": list(host),
        "state": "verified_archive_trees",
        "trees": verified,
        "optional_absent": absent,
        "excluded": ["generated_python_bytecode", ".piopm"],
    }
