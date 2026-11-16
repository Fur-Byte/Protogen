#!/usr/bin/env python3
"""Run P3 host guards and compare complete P2 replays across build modes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/twin"))
import replay
import test_replay


def run(deep):
    output = ROOT / "build/flight-evidence"
    output.mkdir(parents=True, exist_ok=True)
    enabled = ROOT / "build/twin-p3"
    subprocess.run(["make", "-C", str(ROOT / "scripts/twin")], check=True)
    subprocess.run(["make", "-C", str(ROOT / "scripts/twin"), "BUILD=../../build/twin-p3",
                    "EXTRA_FLAGS=-DPROTOGEN_FLIGHT_RECORDER=1", "all", "p3-tests"], check=True)
    for unit in ("unit", "integration"):
        args = [str(enabled / ("flight-recorder-" + unit))]
        if deep:
            args += ["--deep"]
        with (output / f"host-{unit}.log").open("w") as log:
            subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, check=True)
    source = output / ("disabled-deep" if deep else "disabled-fast")
    source.mkdir(exist_ok=True)
    outcomes = test_replay.check_cases(source)
    if deep:
        outcomes.update(test_replay.deep_campaign(source))
    disabled = replay.PROGRAM
    replay.PROGRAM = enabled / "twin"
    parity = []
    for path in sorted(source.glob("*.json")):
        expected = path.with_suffix(".tsv").read_bytes()
        current, timing = replay.execute(json.loads(path.read_text()))
        if current.encode() != expected:
            diff = replay.first_difference(expected.decode(), current)
            raise RuntimeError(f"{path.name}: {diff}\nReproduce: python3 scripts/test-flight-recorder.py")
        parity.append({"scenario": path.stem, "trace_sha256": hashlib.sha256(expected).hexdigest(),
                       "enabled_runtime_seconds": timing["runtime_seconds"]})
    if len(parity) != len(outcomes):
        raise RuntimeError("missing evidence for a parity scenario")
    # Disabled new commands retain the existing unknown-command response.
    replay.PROGRAM = disabled
    trace, _ = replay.execute(test_replay.scenario([[1000, "usb", "RECINFO"]], 10000))
    reply = b"".join(bytes.fromhex(row[-1]) for row in test_replay.rows(trace, "usb"))
    if b"unknown_command" not in reply:
        raise RuntimeError("disabled image exposed recorder commands")

    decode = runpy.run_path(str(ROOT / "scripts/flight-decode.py"))["records"]
    storage = bytearray(128 * 16)
    struct.pack_into("<IIIHH", storage, 127 * 16, 17, 2**32 - 1, 3, 11, 0)
    struct.pack_into("<IIIHH", storage, 0, 24, 0, 1, 2, 0)
    if [r["sequence"] for r in decode(storage, 128, 2, 1, 1)] != [2**32 - 1, 0]:
        raise RuntimeError("serialized ring order or wrap is wrong")
    storage[4] ^= 1
    try:
        decode(storage, 128, 2, 1, 1)
    except ValueError:
        pass
    else:
        raise RuntimeError("decoder accepted incoherent capture")

    evidence = {"deep": deep, "parity": parity, "disabled_scenarios": outcomes,
                "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "working_tree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
                "hardware_time": False}
    (output / ("host-deep.json" if deep else "host-fast.json")).write_text(json.dumps(evidence, indent=2) + "\n")
    print(f"PASS: {len(parity)} byte-exact P2 traces, disabled export guard, decoder, and recorder host tests")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deep", action="store_true")
    run(parser.parse_args().deep)
