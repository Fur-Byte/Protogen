#!/usr/bin/env python3
"""Capture pinned Teensy footprints, BehaviorCore size, and compiler stack frames."""
import argparse
import json
from pathlib import Path
import re
import runpy
import shlex
import subprocess
import sys
import tempfile

from build_support import BuildRun, check_toolchain, find_cli, write_json

ROOT = Path(__file__).resolve().parents[1]
BASE = "f298f3bbf2e07f9a4a4054e1d722123d0d50f34c"
FUNCTIONS = re.compile(r"Behavior::Core::|Behavior::Animation::|Behavior::transitionPose|\brenderFace\(|\brenderFrame\(|\bloop\(|observeExpressionResponse")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-worktree", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/p4-evidence/target.json")
    parser.add_argument("--animation-v2", action="store_true", help="also check the V2 target profiles")
    args = parser.parse_args()
    if args.animation_v2 and args.baseline_worktree:
        parser.error("the P3 baseline predates animation V2")
    source = args.baseline_worktree.resolve() if args.baseline_worktree else ROOT
    if args.baseline_worktree:
        head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
        status = subprocess.check_output(["git", "status", "--porcelain"], cwd=source)
        if head != BASE or status or source == ROOT:
            parser.error("baseline measurement requires the clean P3 worktree")
    features = runpy.run_path(str(source / "scripts/check-features.py"))
    run = BuildRun(source, "behavior-footprint")
    private = None
    try:
        sketch = run.snapshot()
        main_source = sketch / "PROT_OS7_MAX7219.ino"
        original = main_source.read_text()
        cli = find_cli()
        run.record["toolchain"] = check_toolchain(source, cli)
        profiles = [
            ("default", (), ""),
            ("all-recorder", features["ALL_FEATURES"], "-DPROTOGEN_FLIGHT_RECORDER=1"),
        ]
        if args.animation_v2:
            private = tempfile.TemporaryDirectory(prefix="protogen-v2-uart-")
            key = Path(private.name) / "fixture"
            subprocess.run([sys.executable, str(ROOT / "scripts/provision-uart-auth.py"),
                            "--output", str(key)], check=True)
            interactions = ("-DPROTOGEN_FLIGHT_RECORDER=1 -DPROTOGEN_EARLY_EXPRESSION_REVEAL=1 "
                            "-DPROTOGEN_CACHE_FACE=1 -DPROTOGEN_UART_AUTH=1 " + shlex.quote("-I" + str(key)))
            profiles.extend([
                ("v2-default", (), "-DPROTOGEN_ANIMATION_V2=1"),
                ("all-interactions-off", features["ALL_FEATURES"], interactions),
                ("v2-all-interactions", features["ALL_FEATURES"], "-DPROTOGEN_ANIMATION_V2=1 " + interactions),
            ])
        for profile, enabled, flags in profiles:
            main_source.write_text(features["configure_features"](original, enabled, features["ALL_FEATURES"])
                                   if enabled else original)
            artifact = run.compile(cli, sketch, enabled, jobs=4, extra_flags=f"-Werror -fstack-usage {flags}")
            result = run.record["results"][-1]
            result["profile"] = profile
            result["defines"] = flags
            if result["exit_code"] or result.get("dependency_error") or result["warnings"]:
                raise RuntimeError(f"target compilation failed: {run.log}")
            objects = artifact / "objects"
            db = json.loads((objects / "compile_commands.json").read_text())
            nm = Path(db[0]["arguments"][0]).with_name("arm-none-eabi-nm")
            elf = objects / "PROT_OS7_MAX7219.ino.elf"
            symbols = subprocess.check_output([str(nm), "-S", "--defined-only", str(elf)], text=True)
            state = re.search(r"^\w+ (\w+) \w behavior$", symbols, re.M)
            if not state and not args.baseline_worktree:
                raise RuntimeError("linked behavior state missing")
            result["behavior_state_bytes"] = int(state[1], 16) if state else None
            if args.animation_v2:
                animation_linked = "Animation5Table" in symbols
                if animation_linked != profile.startswith("v2-"):
                    raise RuntimeError(f"{profile}: unexpected V2 linkage")
                result["animation_v2_linked"] = animation_linked
                if "interactions" in profile and "protogen_uart_mbedtls_md_hmac_finish" not in symbols:
                    raise RuntimeError("authenticated UART failed to link in the V2 interaction image")
            result["stack_usage"] = [
                line.split("\t", 1)[0].split(":", 1)[-1] + "\t" + line.split("\t", 1)[1]
                for report in sorted(objects.rglob("*.su"))
                for line in report.read_text().splitlines()
                if "\t" in line and FUNCTIONS.search(line)
            ]
            run.publish()
        if not run.finish():
            raise RuntimeError(f"target provenance check failed: {run.log}")
    except Exception as error:
        run.finish(str(error))
        raise
    finally:
        if private:
            private.cleanup()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    write_json(args.output, {"head": run.record["git_head"], "working_tree_dirty": run.record["working_tree_dirty"],
                            "toolchain": run.record["toolchain"], "cases": run.record["results"]})
    for case in run.record["results"]:
        print(case["profile"], case["memory"], "BehaviorCore", case["behavior_state_bytes"])


if __name__ == "__main__":
    main()
