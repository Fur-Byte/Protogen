#!/usr/bin/env python3
"""Check formatting of added and previously formatted first-party source files."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SKETCH = Path("firmware/PROT_OS7_MAX7219")
PYTHON_DIRS = {
    Path(p)
    for p in (
        "scripts",
        "scripts/audio",
        "scripts/tests",
        "scripts/timelines",
        "scripts/twin",
        "verification",
    )
}
CPP_DIRS = {
    SKETCH / "src/AudioV2",
    SKETCH / "src/BehaviorCore",
    SKETCH / "src/HelmetProtocol",
    Path("firmware/esp32/src"),
    Path("scripts/tests"),
    Path("scripts/twin"),
    Path("verification/cbmc"),
    Path("verification/tests"),
}
SKETCH_FILES = {
    "ControllerIdle.h",
    "ResponseTiming.h",
    "Settings.h",
    "SensorHardware.ino",
    "VisorConnector.ino",
}


def formatter(path):
    if path.parent in PYTHON_DIRS and path.suffix == ".py":
        return ["black", "--quiet", "--stdin-filename", str(path), "-"]
    if (path.parent in CPP_DIRS and path.suffix in {".c", ".cpp", ".h", ".hpp"}) or (
        path.parent == SKETCH and path.name in SKETCH_FILES
    ):
        return ["clang-format", f"--assume-filename={path}"]
    if path.parent == Path("tools/helmet") and path.suffix in {".js", ".css", ".html"}:
        return ["prettier", "--stdin-filepath", str(path)]
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", nargs="?", default="origin/main", help="branch or commit to compare with")
    parser.add_argument("--list-tools", action="store_true", help="print just the formatter names needed")
    args = parser.parse_args()
    base = subprocess.check_output(["git", "merge-base", "HEAD", args.base], cwd=ROOT, text=True).strip()
    diff = subprocess.check_output(
        ["git", "diff", "--no-renames", "--diff-filter=AMT", "--name-status", "-z", base, "--"], cwd=ROOT
    )
    fields = os.fsdecode(diff).split("\0")[:-1]
    tools = set()
    failed = False
    for status, name in zip(fields[::2], fields[1::2], strict=True):
        path = Path(name)
        command = formatter(path)
        file = ROOT / path
        if not command or file.is_symlink() or not file.is_file():
            continue
        tools.add(command[0])
        if args.list_tools:
            continue
        current = file.read_bytes()
        if subprocess.check_output(command, cwd=ROOT, input=current) == current:
            continue
        if status == "M":
            previous = subprocess.check_output(["git", "show", f"{base}:{name}"], cwd=ROOT)
            if subprocess.check_output(command, cwd=ROOT, input=previous) != previous:
                continue
        print(f"Format {name} with {command[0]}.", file=sys.stderr)
        failed = True
    if args.list_tools:
        print("\n".join(sorted(tools)))
    return int(failed)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
