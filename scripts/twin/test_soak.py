#!/usr/bin/env python3
"""Focused P8 recovery and campaign accounting regression tests."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import zlib

from replay import PROGRAM, ROOT, encode, execute
from soak import execute_replay, invariant_failure, retain_failure
from test_replay import rows, scenario, state

RUNNER = Path(__file__).with_name("soak.py")


def invoke(*args, expected):
    result = subprocess.run([sys.executable, str(RUNNER), *map(str, args)],
                            cwd=ROOT, capture_output=True, text=True, timeout=45)
    if result.returncode != expected:
        raise AssertionError(f"expected exit {expected}, got {result.returncode}\n{result.stdout}{result.stderr}")


def shard(path, start, count, *, timeout="10", program=PROGRAM):
    invoke("run", "--seed-start", start, "--seed-count", count, "--virtual-duration-ms", 10000,
           "--wall-clock-budget-sec", 25, "--per-seed-timeout-sec", timeout,
           "--program", program, "--output-dir", path, expected=0 if timeout == "10" else 2)
    return json.loads((path / "shard.json").read_text())


def aggregate(path, inputs, expected):
    invoke("aggregate", "--seed-start", 0, "--seed-count", 2, "--virtual-duration-ms", 10000,
           "--output-dir", path, *inputs, expected=expected)
    return json.loads((path / "aggregate.json").read_text())


def campaign(evidence, scratch):
    both = shard(evidence / "fast-shard", 0, 2)
    second = shard(scratch / "second", 1, 1)
    assert both["seeds"][1]["semantic_sha256"] == second["seeds"][0]["semantic_sha256"]
    for seed in both["seeds"]:
        assert seed["virtual_duration_us"] == 10_000_000
        assert seed["queue_peaks"]["audio_ring_peak"] == 512
        assert seed["queue_peaks"]["uart_wire_in_peak"] == 512
        assert seed["drop_and_rejection_counts"]["audio_dropped"] > 0
        assert seed["event_counts"]["lost_acks"] == 1
        assert seed["event_counts"]["uart_duplicate_acks"] == 1
        assert seed["event_counts"]["uart_expired_acks"] == 2
    full = evidence / "fast-shard" / "shard.json"
    single = scratch / "second" / "shard.json"
    report = aggregate(evidence / "fast-aggregate", [full], 0)
    assert report["coverage"]["passed_seed_count"] == 2 and not report["architecture_target_met"]
    report = aggregate(scratch / "missing", [single], 2)
    assert report["coverage"]["missing_seeds"] == [0]
    report = aggregate(scratch / "no-shards", [], 2)
    assert report["coverage"]["missing_seeds"] == [0, 1]
    report = aggregate(scratch / "duplicates", [full, single], 1)
    assert report["coverage"]["duplicate_seeds"] == [1]
    unfinished = scratch / "unfinished.json"
    unfinished.write_text(json.dumps({**both, "status": "INCOMPLETE"}))
    report = aggregate(scratch / "unfinished", [unfinished], 2)
    assert report["coverage"]["passed_seed_count"] == 2 and report["unfinished_shards"] == [str(unfinished)]
    tampered = scratch / "tampered.json"
    both["seeds"][0]["virtual_duration_us"] -= 1
    tampered.write_text(json.dumps(both))
    report = aggregate(scratch / "tamper", [tampered], 1)
    assert report["coverage"]["passed_seed_count"] == 0
    timed = shard(evidence / "timeout-shard", 0, 2, timeout="0.001")
    assert [seed["status"] for seed in timed["seeds"]] == ["INCOMPLETE", "INCOMPLETE"]
    assert all(seed["reason"] == "seed-timeout" and seed["virtual_duration_us"] is None
               for seed in timed["seeds"])
    report = aggregate(scratch / "timeout-aggregate", [evidence / "timeout-shard" / "shard.json"], 2)
    assert report["coverage"]["incomplete_seeds"] == [0, 1]
    noisy = scratch / "noisy-twin"
    noisy.write_text("#!/usr/bin/env python3\nimport sys\nsys.stdout.write('x' * 4000000)\n")
    noisy.chmod(0o700)
    invoke("run", "--seed-start", 0, "--seed-count", 1, "--virtual-duration-ms", 10000,
           "--wall-clock-budget-sec", 5, "--per-seed-timeout-sec", 5, "--program", noisy,
           "--output-dir", scratch / "noisy", expected=1)
    rejected = json.loads((scratch / "noisy/shard.json").read_text())
    assert rejected["seeds"][0]["reason"] == "output-limit"


def configuration(evidence):
    images = (ROOT / "scripts/tests/fixtures/eeprom-v1.hex").read_text().splitlines()
    oldest = bytes.fromhex(images[0])
    old = slot(bytes.fromhex(images[2]), 0xffffffff, 4)
    new = slot(bytes.fromhex(images[3]), 0, 2)
    invalid_settings = slot(new, 0, 6)
    images_and_expected = {
        "legacy-format-1": (oldest + b"\xff" * 64, 4),
        "sequence-wrap": (old + new, 2),
        "crc-corrupt": (old + flipped(new, 48), 4),
        "marker-corrupt": (old + flipped(new, 52), 4),
        "invalid-setting-with-valid-crc": (old + invalid_settings, 4),
    }
    results = {}
    for name, (image, expression) in images_and_expected.items():
        replay = scenario([], 20_000, eeprom=image.hex())
        trace, _ = execute(replay)
        assert int(state(trace)[3]) == expression, name
        assert rows(trace, "eeprom")[0][-1] == image.hex(), name
        results[name] = {"input": replay, "expected_expression": expression, "observed_expression": int(state(trace)[3])}
    (evidence / "eeprom.json").write_text(json.dumps(results, indent=2) + "\n")


def slot(fixture, sequence, expression):
    result = bytearray(fixture)
    result[8:12] = sequence.to_bytes(4, "little")
    result[14:16] = expression.to_bytes(2, "little")
    result[48:52] = zlib.crc32(result[:48]).to_bytes(4, "little")
    return bytes(result)


def flipped(original, offset):
    result = bytearray(original)
    result[offset] ^= 1
    return bytes(result)


def failure_reproduction(evidence):
    output = evidence / "expected-failure"
    output.mkdir()
    replay = scenario([[30000, "expect", ["expression", 4]], [50000, "boop", 200]], 100000)
    encoded = encode(replay, soak=True).encode()
    deadline = time.monotonic() + 5
    code, outcome, _, stderr, _ = execute_replay(PROGRAM, encoded, deadline)
    failure = invariant_failure(stderr)
    assert code == 3 and outcome is None
    assert failure == {"virtual_us": 30000, "input_sequence": 1, "invariant": "expression", "expected": 4, "observed": 0}
    retain_failure(output, PROGRAM, replay, encoded, {"seed": 19, "first_divergence": failure}, deadline)
    result = json.loads((output / "failure.json").read_text())
    assert result["tail_complete"] and "reproduce" in result and "reduced_reproduce" in result
    prefix = json.loads((output / "failure-prefix.json").read_text())
    assert prefix["events"] == replay["events"][:1] and prefix["duration_us"] == 30000


def terminal_adc_failure(evidence):
    replay = scenario([
        [20_000, "audio-invalid-pin", 1], [20_000, "visor", 1], [220_000, "visor", 0],
        [500_000, "expect", ["audio-recovery-state", 2]],
        [510_000, "expect", ["audio-failure-reason", 5]],
        [520_000, "audio-invalid-pin", 0],
        [2_000_000, "expect", ["audio-retries", 0]], [2_010_000, "expect", ["audio-running", 0]],
        [2_030_000, "usb", "EXPR 3"], [2_100_000, "expect", ["expression", 3]]
    ], 2_200_000)
    replay_case(evidence, "terminal-adc-failure", replay)


def replay_case(evidence, name, replay, *, program=PROGRAM, asset=None, expected=0):
    path = evidence / f"{name}.input.json"
    path.write_text(json.dumps(replay, indent=2) + "\n")
    arguments = [] if asset is None else ["--asset", asset]
    invoke("replay", path, "--program", program, "--wall-clock-budget-sec", 5,
           "--output-dir", evidence / name, *arguments, expected=expected)
    return json.loads((evidence / name / "replay.json").read_text())


def experimental(evidence, program):
    sys.path.insert(0, str(ROOT / "scripts"))
    from timelines.compiler import compile_document

    source = json.loads((ROOT / "tools/helmet/timelines/three-channel.json").read_text())
    asset = evidence / "timeline.bin"
    asset.write_bytes(compile_document(source).data)
    steps = [
        (130, "expect", ["connector-ready", 1]), (150, "expect", ["timeline-state", 1]),
        (170, "audio", 250), (330, "expect", ["speaking", 1]),
        (350, "hold", 275000), (620, "audio", 0),
        (640, "expect", ["speaking", 0]), (650, "expect", ["mouth-aperture", 0]),
        # The same foreground stall also expires the protected dock's rail measurement.
        (660, "expect", ["connector-ready", 0]),
        (1230, "expect", ["connector-ready", 1]), (1240, "audio", 250),
        (1410, "expect", ["speaking", 1]), (1420, "boop", 100),
        (1445, "expect", ["boop-active", 1]), (1450, "expect", ["speaking", 0]),
        (1460, "boop", 1023), (1460, "audio", 0), (1540, "expect", ["mouth-aperture", 0]),
        (1620, "timeline-cancel", 0), (1650, "expect", ["timeline-state", 3]),
        *[(1680, "v2-track", [0, 0, 5, 0, 500, -1, 1]) for _ in range(9)],
        (1700, "expect", ["rejected-tracks", 1]),
        (1730, "dock-pilot", 1), (1750, "expect", ["connector-ready", 0]),
        (1750, "expect", ["dock-buffer-enabled", 0]), (1760, "dock-pilot", 0),
        (1760, "dock-voltage", 480), (1860, "expect", ["sensor-faults", 1]),
        (1880, "dock-voltage", 640), (2530, "expect", ["connector-ready", 1]),
        (2540, "expect", ["sensor-faults", 0]), (2550, "expect", ["audio-running", 1]),
        (2560, "expect", ["speaking", 0]),
        (2580, "audio", 250), (2750, "expect", ["speaking", 1]),
        (2760, "audio-stall", 1), (2760, "audio", 0),
        (2870, "expect", ["sensor-faults", 4]), (2880, "expect", ["speaking", 0]),
        (2890, "expect", ["connector-ready", 1]), (2900, "audio-stall", 0),
        (3110, "expect", ["audio-running", 1]), (3120, "expect", ["sensor-faults", 0]),
        (3130, "expect", ["speaking", 0]), (3140, "expect", ["mouth-aperture", 0]),
    ]
    replay = scenario([[at * 1000, operation, value] for at, operation, value in steps], 3_200_000)
    result = replay_case(evidence, "experimental-recovery", replay, program=program, asset=asset)
    flags = result["feature_flags"]
    assert flags["audio_v2"] and flags["animation_v2"] and flags["animation_timelines"] and flags["visor_connector"]
    assert result["drop_and_rejection_counts"]["audio_dropped"] > 0
    truncated = evidence / "invalid-timeline.bin"
    truncated.write_bytes(asset.read_bytes()[:-1])
    result = replay_case(evidence, "invalid-timeline", scenario([], 20000),
                         program=program, asset=truncated, expected=1)
    assert "invalid compiled timeline" in result["diagnostic"]
    result = replay_case(evidence, "timeline-disabled", scenario([], 20000), asset=asset, expected=1)
    assert "timeline support disabled" in result["diagnostic"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, default=ROOT / "build/p8-faults")
    parser.add_argument("--experimental-program", type=Path)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="protogen-fault-tests-") as temporary:
        campaign(evidence, Path(temporary))
    configuration(evidence)
    failure_reproduction(evidence)
    terminal_adc_failure(evidence)
    if args.experimental_program:
        experimental(evidence, args.experimental_program)
    print(f"P8 recovery and campaign checks passed. Evidence in {evidence}")


if __name__ == "__main__":
    main()
