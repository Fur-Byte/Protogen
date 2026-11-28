#!/usr/bin/env python3
"""First-party entry point for Protogen's existing and experimental toolchains."""

from __future__ import annotations

import argparse
import json
import importlib.util
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(script: str, *arguments: str) -> int:
    return subprocess.call([sys.executable, str(ROOT / script), *arguments], cwd=ROOT)


def doctor() -> int:
    result = {
        "python": sys.version.split()[0],
        "host": sys.platform,
        "git": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "native_setup": (ROOT / "build/native-python/native-setup.json").is_file(),
        "reference_setup": (ROOT / "build/toolchains/environment.sh").is_file(),
        "production_qualified": False,
    }
    print(json.dumps(result, indent=2))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    setup = commands.add_parser("setup")
    setup.add_argument("--backend", choices=("reference", "native"), default="reference")
    setup.add_argument("--wheelhouse", type=Path)
    setup.add_argument("--archives", type=Path)
    commands.add_parser("doctor")
    build = commands.add_parser("build")
    build.add_argument("role", choices=("teensy", "helmet", "paw", "all"))
    build.add_argument("--backend", choices=("reference", "native"), default="reference")
    build.add_argument("--output", type=Path)
    build.add_argument("--jobs", type=int, choices=range(1, 33), default=4)
    build.add_argument("--reproducible", action="store_true")
    build.add_argument("--reference", type=Path)
    test = commands.add_parser("test")
    test.add_argument("--all", action="store_true", required=True)
    verify = commands.add_parser("verify")
    verify.add_argument("--formal", action="store_true", required=True)
    verify.add_argument("--tier", choices=("fast", "extended"), default="fast")
    audit = commands.add_parser("audit")
    audit.add_argument("--dependencies", action="store_true", required=True)
    qualify = commands.add_parser("qualify")
    qualify.add_argument("manifest", type=Path)
    qualify.add_argument("--artifact-root", type=Path, required=True)
    qualify.add_argument("--source", required=True)
    qualify.add_argument("--candidate", type=Path)
    release = commands.add_parser("release")
    release.add_argument("--verify", action="store_true", required=True)
    release.add_argument("archive", type=Path)
    release.add_argument("--sha256", required=True, help="Independently retained archive digest")
    args = parser.parse_args()
    if args.command == "doctor":
        return doctor()
    if args.command == "setup":
        if args.backend == "reference":
            if args.wheelhouse or args.archives:
                parser.error("Explicit caches are supported by the native setup only")
            return run("scripts/install-toolchains.py")
        extra = [
            item
            for flag, value in (("--wheelhouse", args.wheelhouse), ("--archives", args.archives))
            if value
            for item in (flag, str(value.absolute()))
        ]
        return run("scripts/native_esp.py", "setup", *extra)
    if args.command == "build":
        if args.backend == "reference":
            if args.output or args.reproducible or args.reference:
                parser.error("Native build options require --backend native")
            if args.role in ("teensy", "all"):
                if result := run("scripts/build.py"):
                    return result
            if args.role in ("helmet", "paw", "all"):
                # The reference wrapper intentionally compiles both roles.
                return run("scripts/build-esp32.py")
            return 0
        if args.role == "teensy":
            parser.error("Native ESP backend is not a Teensy backend")
        if not args.output:
            parser.error("Native builds require a fresh --output directory")
        for role in ("helmet", "paw") if args.role == "all" else (args.role,):
            path = args.output.absolute() / role if args.role == "all" else args.output.absolute()
            extra = ["--output", str(path), "--jobs", str(args.jobs)]
            if args.reproducible:
                extra += ["--reproducible"]
            if args.reference:
                ref = args.reference.absolute() / role if args.role == "all" else args.reference.absolute()
                extra += ["--reference", str(ref)]
            if result := run("scripts/native_esp.py", "build", role, *extra):
                return result
        return 0
    if args.command == "test":
        return run("scripts/check-baseline.py")
    if args.command == "verify":
        return run("verification/run.py", "--tier", args.tier)
    if args.command == "audit":
        for script in (
            "test-dependency-integrity.py",
            "test-esp-source-integrity.py",
            "test-esp-tool-integrity.py",
            "test-locked-esp-installer.py",
            "test-native-esp.py",
        ):
            if result := run("scripts/tests/" + script):
                return result
        return 0
    if args.command == "release":
        spec = importlib.util.spec_from_file_location("package_release", ROOT / "scripts/package-release.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        try:
            module.validate_previous(args.archive.read_bytes(), args.sha256)
        except (OSError, ValueError, RuntimeError, KeyError, TypeError) as error:
            parser.exit(1, f"Release verification refused: {error}\n")
        print("Development recovery archive verified against retained digest; no production approval")
        return 0
    if args.command == "qualify":
        extra = ["--candidate", str(args.candidate.absolute())] if args.candidate else []
        return run(
            "scripts/qualification.py",
            "verify",
            str(args.manifest.absolute()),
            "--artifact-root",
            str(args.artifact_root.absolute()),
            "--source",
            args.source,
            *extra,
        )
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
