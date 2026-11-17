#!/usr/bin/env python3
"""Verify isolated supervisor/recorder host parity and Teensy linkage."""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import re
import runpy
import subprocess

from build_support import BuildRun, check_toolchain, find_cli, write_json

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/p8"
PROFILE = "-DPROTOGEN_ANIMATION_V2=1 -DPROTOGEN_ANIMATION_TIMELINES=1 -DPROTOGEN_AUDIO_V2=1 " \
          "-DPROTOGEN_CACHE_FACE=1 -DPROTOGEN_EARLY_EXPRESSION_REVEAL=1"


def defines(observer, recorder):
    return f"-DPROTOGEN_SUPERVISION_OBSERVE={observer} -DPROTOGEN_FLIGHT_RECORDER={recorder} {PROFILE}"


def profiles():
    return itertools.product(range(2), repeat=2)


def host():
    unit = OUT / "supervision-unit"
    subprocess.run([os.getenv("CXX", "clang++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-I", str(ROOT / "firmware/PROT_OS7_MAX7219"),
                    str(ROOT / "scripts/tests/supervision-unit.cpp"), "-o", str(unit)], check=True)
    subprocess.run([str(unit)], check=True)
    reference = None
    entries = []
    for observer, recorder in profiles():
        case = f"{observer}{recorder}"
        relative = f"build/p8-supervision-{case}"
        with (OUT / f"supervision-host-{case}.log").open("w") as stream:
            subprocess.run(["make", "-C", str(ROOT / "scripts/twin"), "-f", "Makefile",
                            "-f", "../supervision.mk", "BUILD=../../" + relative,
                            "EXTRA_FLAGS=" + defines(observer, recorder), "supervision-integration"],
                           stdout=stream, stderr=subprocess.STDOUT, check=True, timeout=180)
        trace = subprocess.check_output([str(ROOT / relative / "supervision-integration")], timeout=30)
        (OUT / f"supervision-trace-{case}.txt").write_bytes(trace)
        if reference is None:
            reference = trace
        if reference != trace:
            raise RuntimeError(f"supervision profile {case} changed modeled output")
        entries.append({"observer": observer, "recorder": recorder, "trace_sha256": hashlib.sha256(trace).hexdigest()})
        print(f"PASS: supervision host {case}, exact legacy-observable output")
    write_json(OUT / "supervision-host.json", {"profiles": entries})


def target():
    run = BuildRun(ROOT, "p8-supervision")
    feature = runpy.run_path(str(ROOT / "scripts/check-features.py"))
    try:
        sketch = run.snapshot()
        source = sketch / "PROT_OS7_MAX7219.ino"
        enabled = feature["ALL_FEATURES"]
        source.write_text(feature["configure_features"](source.read_text(), enabled, enabled))
        cli = find_cli()
        run.record["toolchain"] = check_toolchain(ROOT, cli)
        for observer, recorder in profiles():
            case = f"{observer}{recorder}"
            artifact = run.compile(cli, sketch, enabled, extra_flags="-Werror -fstack-usage " +
                                   defines(observer, recorder), jobs=4)
            row = run.record["results"][-1]
            row["profile"] = case
            if row["exit_code"] or row.get("dependency_error") or row["warnings"]:
                raise RuntimeError(f"{case}: target build failed, see {run.log}")
            objects = artifact / "objects"
            units = json.loads((objects / "compile_commands.json").read_text())
            nm = Path(units[0]["arguments"][0]).with_name("arm-none-eabi-nm")
            elf = objects / "PROT_OS7_MAX7219.ino.elf"
            symbols = subprocess.check_output([str(nm), "-C", "-S", "--defined-only", str(elf)], text=True)
            state = re.search(r"^\w+ (\w+) [bBdD] supervision$", symbols, re.M)
            if bool(state) != bool(observer) or ("P3Teensy::ring" in symbols) != bool(recorder):
                raise RuntimeError(f"{case}: observer/recorder linkage mismatch")
            row["observer_bytes"] = int(state[1], 16) + 4 if state else 0
            row["stack_usage"] = [line for report in objects.rglob("*.su")
                                  for line in report.read_text().splitlines()
                                  if any(mark in line for mark in ("observeProgress", "Observer::", "void loop()"))]
            run.publish()
            print(f"PASS: supervision target {case}")
        if not run.finish():
            raise RuntimeError(f"target provenance check failed, see {run.log}")
        write_json(OUT / "supervision-target.json", run.record)
    except Exception as error:
        run.finish(str(error))
        raise


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", action="store_true")
    arguments = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    (target if arguments.target else host)()
