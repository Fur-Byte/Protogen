#!/usr/bin/env python3
"""Validate a P2 replay and drive the host-only sketch executable."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
PROGRAM = ROOT / "build/twin/twin"
NUMERIC_BOUNDS = {
    "visor": (0, 1), "button": (0, 1), "boop": (0, 1023), "sample": (0, 1023),
    "audio": (0, 1023), "sensor-stall": (0, 1), "audio-stall": (0, 1),
    "hold": (1, 120_000_000), "disconnect": (0, 1), "usb-disconnect": (0, 1),
    "short-write": (0, 32), "power-cut-after": (0, 1080), "hello": (1, 2**32 - 1),
    "expression": (0, 5), "duplicate": (0, 0), "timeline-cancel": (0, 0),
}
METRICS = (
    "inputs_processed", "loops", "adc_samples", "uart_wire_in_peak", "uart_wire_out_peak",
    "uart_rx_peak", "usb_rx_peak", "audio_ring_peak", "uart_wire_dropped",
    "uart_rx_dropped", "uart_peer_dropped", "usb_rx_dropped", "audio_dropped",
)


def integer(value, low, high, name):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in [{low}, {high}]")
    return value


def encode(replay, reference=False):
    required = {"version", "origin_us", "duration_us", "loop_us", "cosmetic_seed", "epoch", "events"}
    if (not isinstance(replay, dict) or not required <= replay.keys()
            or set(replay) - required - {"eeprom", "cosmetic_tape", "audio_tape"}):
        raise ValueError("invalid replay fields")
    integer(replay["version"], 1, 1, "version")
    origin = integer(replay["origin_us"], 0, 2**48, "origin_us")
    duration = integer(replay["duration_us"], 1000, 120_000_000, "duration_us")
    tick = integer(replay["loop_us"], 100, 20_000, "loop_us")
    seed = integer(replay["cosmetic_seed"], 0, 2**32 - 1, "cosmetic_seed")
    epoch = integer(replay["epoch"], 1, 2**32 - 1, "epoch")
    tape, image = replay.get("cosmetic_tape", []), replay.get("eeprom", "-")
    audio = replay.get("audio_tape", [])
    if not isinstance(tape, list) or len(tape) > 4096:
        raise ValueError("cosmetic_tape must contain at most 4096 words")
    if not isinstance(image, str) or (image != "-" and not re.fullmatch(r"[a-fA-F0-9]{256}", image)):
        raise ValueError("eeprom must contain two 64-byte slots in hex")
    if not isinstance(audio, list) or len(audio) > 32768:
        raise ValueError("audio_tape must contain at most 32768 samples")
    lines = [" ".join(map(str, (origin, duration, tick, seed, epoch, int(not reference), image, len(tape), len(audio))))]
    lines += [str(integer(word, 0, 2**32 - 1, "cosmetic word")) for word in tape]
    lines += [str(integer(sample, 0, 1023, "audio sample")) for sample in audio]
    events = replay["events"]
    if not isinstance(events, list) or len(events) > 4096:
        raise ValueError("events must contain at most 4096 entries")
    previous = 1000
    for sequence, event in enumerate(events, 1):
        if not isinstance(event, list) or len(event) != 3:
            raise ValueError(f"event {sequence} must contain [time_us, operation, value]")
        at, operation, value = event
        previous = integer(at, previous, duration, f"event {sequence} time")
        if operation == "usb":
            if not isinstance(value, str) or not re.fullmatch(r"[ -~]{1,512}", value):
                raise ValueError(f"event {sequence}: USB command must be one printable line")
            argument = (value + "\n").encode("ascii").hex()
        elif operation == "uart":
            if not isinstance(value, str) or not re.fullmatch(r"(?:[a-fA-F0-9]{2}){1,2048}", value):
                raise ValueError(f"event {sequence}: UART value must be 1–2048 bytes in hex")
            argument = value.lower()
        elif operation in ("v2-track", "v2-cancel"):
            bounds = ((0, 3), (0, 5), (0, 7), (0, 4), (0, 65535), (-8, 8), (-8, 8)) if operation == "v2-track" else ((0, 7), (1, 2**32 - 1))
            if not isinstance(value, list) or len(value) != len(bounds):
                raise ValueError(f"event {sequence}: {operation} expects {len(bounds)} numeric fields")
            argument = ",".join(str(integer(number, *limits, f"event {sequence} field {index}"))
                                for index, (number, limits) in enumerate(zip(value, bounds)))
        elif isinstance(operation, str) and operation in NUMERIC_BOUNDS:
            argument = integer(value, *NUMERIC_BOUNDS[operation], f"event {sequence} argument")
        else:
            raise ValueError(f"event {sequence}: unknown operation {operation!r}")
        lines.append(f"{at} {sequence} {operation} {argument}")
    return "\n".join(lines) + "\n"


def execute(replay, reference=False, *, program=None, asset=None):
    encoded = encode(replay, reference)
    start = time.monotonic()
    command = [str(program or PROGRAM)] + ([str(asset)] if asset is not None else [])
    result = subprocess.run(command, input=encoded, text=True, capture_output=True, timeout=120)
    runtime = time.monotonic() - start
    if result.returncode:
        raise ValueError(f"runner exited {result.returncode}: {result.stderr.strip()}")
    rows = [line.split("\t") for line in result.stdout.splitlines()]
    totals = rows[-1]
    if totals[1] != "totals" or len(totals) != 3 + len(METRICS):
        raise ValueError("incomplete runner trace")
    metrics = dict(zip(METRICS, map(int, totals[3:])))
    rss = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
    metrics.update(trace_bytes=len(result.stdout), child_peak_rss_bytes=rss * (1 if sys.platform == "darwin" else 1024),
                   virtual_us=int(totals[0]), runtime_seconds=runtime, trace_records=len(rows),
                   skipped_loop_opportunities=int(result.stderr.strip().split("=")[-1]))
    return result.stdout, metrics


def first_difference(expected, actual):
    expected_lines, actual_lines = expected.splitlines(), actual.splitlines()
    for line in range(max(len(expected_lines), len(actual_lines))):
        left = expected_lines[line] if line < len(expected_lines) else "<end>"
        right = actual_lines[line] if line < len(actual_lines) else "<end>"
        if left != right:
            return f"trace record {line + 1} (time, type, input sequence in first fields)\nexpected {left}\nactual   {right}"
    return "line ending differs"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    global PROGRAM
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--animation-v2", action="store_true", help="use build/twin-v2/twin")
    parser.add_argument("--reference", action="store_true")
    parser.add_argument("--expect", type=Path, help="compare with explicitly supplied expected TSV")
    parser.add_argument("--trace", type=Path, help="save actual trace")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    command = f"python3 scripts/twin/replay.py {str(args.input)!r}"
    if args.animation_v2:
        PROGRAM = ROOT / "build/twin-v2/twin"
        command += " --animation-v2"
    if args.reference:
        command += " --reference"
    if args.expect:
        command += f" --expect {str(args.expect)!r}"
    try:
        replay = json.loads(args.input.read_text())
        trace, metrics = execute(replay, args.reference)
        originals = {path.resolve() for path in (args.input, args.expect) if path}
        outputs = [path.resolve() for path in (args.trace, args.report) if path]
        for index, output in enumerate(outputs):
            protected = [*originals, *outputs[:index]]
            if any(output == path or (output.exists() and path.exists() and output.samefile(path))
                   for path in protected):
                raise ValueError("input, expected trace, actual trace, and report paths must be distinct")
        if args.trace:
            args.trace.write_text(trace)
        elif not args.expect:
            sys.stdout.write(trace)
        if args.expect:
            expected = args.expect.read_bytes()
            if expected != trace.encode():
                raise ValueError(first_difference(expected.decode(errors="replace"), trace))
        if args.report:
            firmware = ROOT / "firmware/PROT_OS7_MAX7219"
            identity = {
                "version": 1, "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "working_tree_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
                "executable_sha256": digest(PROGRAM), "input_sha256": digest(args.input),
                "trace_sha256": hashlib.sha256(trace.encode()).hexdigest(),
                "asset_sha256": digest(firmware / "Icons.h"),
                "config_sha256": digest(ROOT / "tools/helmet/config-metadata.json"),
                "features": ["Microphone", "BoopSensor", "VisorSensor", "WirelessPaw"],
                "efficiency_flags": 0, "uart_auth": False, "fidelity": "F0–F2",
                "execution": "reference" if args.reference else "accelerated",
                "metrics": metrics,
            }
            if args.animation_v2:
                identity["animation_v2"] = True
            args.report.write_text(json.dumps(identity, indent=2) + "\n")
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"{error}\nReproduce: {command}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
