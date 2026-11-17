#!/usr/bin/env python3
"""Compare the shared core with independently captured P3 host traces."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = "f298f3bbf2e07f9a4a4054e1d722123d0d50f34c"
FIXTURE = ROOT / "scripts/tests/fixtures/behavior-p3.json.gz"
PROFILES = {
    "default": (),
    "recorder": ("PROTOGEN_FLIGHT_RECORDER",),
    "early": ("PROTOGEN_EARLY_EXPRESSION_REVEAL",),
    "cache": ("PROTOGEN_CACHE_FACE",),
    "early-cache": ("PROTOGEN_EARLY_EXPRESSION_REVEAL", "PROTOGEN_CACHE_FACE"),
}


def load_harness(root):
    sys.path.insert(0, str(root / "scripts/twin"))
    import replay
    import test_replay
    return replay, test_replay


def build(root, profile):
    directory = "twin" if profile == "default" else f"twin-behavior-{profile}"
    options = ["BUILD=../../build/" + directory,
               "EXTRA_FLAGS=" + " ".join(f"-D{flag}=1" for flag in PROFILES[profile])]
    subprocess.run(["make", "-C", str(root / "scripts/twin"), *options], check=True)
    return root / "build" / directory / "twin"


def extra_inputs(test_replay):
    # At 243 ms the staged USB command, UART expression, and debounced
    # local release are all eligible. REC rows expose the individual starts.
    collision = [[120000, "hello", 1], [150000, "button", 0], [218000, "button", 1],
                 [240000, "expression", 2], [243000, "usb", "EXPR 3"],
                 [249000, "usb", "EXPR 1"], [278000, "usb", "LATENCY"],
                 [330000, "usb", "STATUS"], [390000, "usb", "LATENCY"],
                 [455000, "usb", "PERF"]]
    recorded = collision + [[500000, "usb", "RECINFO"]]
    recorded += [[530000 + i * 10000, "usb", f"REC {i}"] for i in range(18)]
    recorded += [[720000, "usb", "RECSTAT 2"], [745000, "usb", "RECSTAT 4"]]
    yield "collision", "recorder", test_replay.scenario(recorded, 780000)
    # Drive audio activity across retarget, boop and hide/wake boundaries.
    # All optional render modes use these same samples and inputs.
    animation = [[1000, "usb", "SET blink_interval_ms=1000 blink_jitter_ms=0 boop_mode=1 boop_duration_ms=400"],
                 [175000, "audio", 300], [280000, "usb", "EXPR 2"], [313000, "usb", "EXPR 3"],
                 [320000, "usb", "EXPR 0"], [480000, "boop", 100], [520000, "usb", "EXPR 5"],
                 [545000, "visor", 1], [920000, "boop", 1023], [940000, "visor", 0],
                 [1250000, "usb", "LATENCY"], [1350000, "audio", 0], [1450000, "usb", "STATUS"],
                 [2250000, "usb", "PERF"]]
    for profile in ("default", "early", "cache", "early-cache"):
        yield "animation-" + profile, profile, test_replay.scenario(
            animation, 2300000, origin=(2**32 - 450) * 1000, cosmetic_tape=[5, 256, 42, 0])


def capture(root, output):
    head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    status = subprocess.check_output(["git", "-C", str(root), "status", "--porcelain", "--untracked-files=all"])
    if head != BASE or root == ROOT or status:
        raise ValueError("capture requires a clean, separate worktree at " + BASE)
    if output.exists() or output.resolve() == FIXTURE.resolve():
        raise ValueError("capture requires a new path separate from the tracked fixture")
    replay, test_replay = load_harness(root)
    cases = []
    replay.PROGRAM = build(root, "default")
    with tempfile.TemporaryDirectory(prefix="protogen-p4-reference-") as directory:
        evidence = Path(directory)
        test_replay.check_cases(evidence)
        test_replay.deep_campaign(evidence)
        for path in sorted(evidence.glob("*.json")):
            cases.append({
                "name": path.stem, "profile": "default", "deep": path.stem.startswith("seed-"),
                "input": json.loads(path.read_text()), "expected": path.with_suffix(".tsv").read_text(),
            })
    binaries = {}
    for name, profile, scenario in extra_inputs(test_replay):
        if profile not in binaries:
            binaries[profile] = build(root, profile)
        replay.PROGRAM = binaries[profile]
        trace, _ = replay.execute(scenario)
        cases.append({"name": name, "profile": profile, "deep": False, "input": scenario, "expected": trace})
    data = {"version": 1, "source_commit": head, "cases": cases}
    output.write_bytes(gzip.compress(json.dumps(data, separators=(",", ":")).encode(), mtime=0))
    print(f"Captured {len(cases)} complete P3 traces from {head}")


def replay_rows(trace, kind):
    return [row.split("\t") for row in trace.splitlines() if row.split("\t", 2)[1] == kind]


def verify(deep, evidence):
    data = json.loads(gzip.decompress(FIXTURE.read_bytes()))
    if data["version"] != 1 or data["source_commit"] != BASE:
        raise ValueError("unexpected P3 trace provenance")
    replay, _ = load_harness(ROOT)
    evidence.mkdir(parents=True, exist_ok=True)
    p4 = ROOT / "scripts/tests/fixtures/p4-legacy-traces.json"
    capture = json.loads(p4.read_text()) if p4.is_file() else None
    p4_hashes = {(c["case"], c["profile"]): c["trace_sha256"] for c in capture["cases"]} if capture else {}
    if capture and capture["source_commit"] != "25eef9b4ccf8171338b646a2927f9d8e77aed123":
        raise ValueError("unexpected P4 trace provenance")
    binaries = {}
    results = []
    for case in data["cases"]:
        if case["deep"] and not deep:
            continue
        name, profile = case["name"], case["profile"]
        if profile not in binaries:
            binaries[profile] = build(ROOT, profile)
        replay.PROGRAM = binaries[profile]
        trace, metrics = replay.execute(case["input"])
        if trace != case["expected"]:
            (evidence / f"{name}-input.json").write_text(json.dumps(case["input"], indent=2) + "\n")
            (evidence / f"{name}-expected.tsv").write_text(case["expected"])
            (evidence / f"{name}-actual.tsv").write_text(trace)
            raise ValueError(f"{name} ({profile}): {replay.first_difference(case['expected'], trace)}")
        if name == "collision":
            replies = b"".join(bytes.fromhex(row[-1]) for row in replay_rows(trace, "usb"))
            records = [reply["record"] for line in replies.splitlines()
                       if "record" in (reply := json.loads(line))]
            details = [record[3] for record in records if record[1:3] == [243000, 2]]
            if details != [0x103, 0x202, 0x003]:
                raise ValueError(f"{name}: expected USB, UART, local admissions in order, got {details}")
        if name.startswith("animation-") and not any(row[7] == "1" for row in replay_rows(trace, "state")):
            raise ValueError(f"{name}: no active speech frames")
        digest = hashlib.sha256(trace.encode()).hexdigest()
        if capture and p4_hashes.get((name, profile)) != digest:
            raise ValueError(f"{name} ({profile}): trace digest differs from the independently captured P4 output")
        results.append({
            "case": name, "profile": profile, "trace_sha256": digest,
            "host_metrics": metrics,
        })
    report = {"reference_sha": BASE, "reference_fixture_sha256": hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),
              "head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
              "cases": results}
    if capture:
        report["p4_reference_sha"] = capture["source_commit"]
    (evidence / ("deep.json" if deep else "fast.json")).write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(results)} byte-exact P3 reference traces, including diagnostic rows")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deep", action="store_true")
    parser.add_argument("--evidence", type=Path, default=ROOT / "build/p4-evidence")
    parser.add_argument("--capture-reference", type=Path)
    parser.add_argument("--output", type=Path, help="new file for an explicit baseline capture")
    args = parser.parse_args()
    try:
        if args.capture_reference:
            if not args.output:
                parser.error("--capture-reference requires --output")
            capture(args.capture_reference.resolve(), args.output)
        elif args.output:
            parser.error("--output requires --capture-reference")
        else:
            verify(args.deep, args.evidence)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"{error}\nReproduce: python3 scripts/check-behavior-parity.py" +
                    (" --deep\n" if args.deep else "\n"))


if __name__ == "__main__":
    main()
