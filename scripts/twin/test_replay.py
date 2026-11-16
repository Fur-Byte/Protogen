#!/usr/bin/env python3
"""Independent assertions on replay output and equivalence with reference stepping."""
import argparse
import copy
import json
from pathlib import Path
import random
import sys
import time

from replay import ROOT, encode, execute, first_difference

HERE = Path(__file__).resolve().parent


def require(condition, reason):
    if not condition:
        raise AssertionError(reason)


def scenario(events, duration=1_000_000, origin=0, seed=19, epoch=1, **initial):
    return dict(version=1, origin_us=origin, duration_us=duration, loop_us=1000,
                cosmetic_seed=seed, epoch=epoch, events=events, **initial)


def rows(trace, kind):
    return [line.split("\t") for line in trace.splitlines() if line.split("\t", 2)[1] == kind]


def state(trace):
    return rows(trace, "state")[-1]


def run(replay, evidence, name, reference=True):
    trace, metrics = execute(replay)
    path = evidence / f"{name}.json"
    path.write_text(json.dumps(replay, indent=2) + "\n")
    (evidence / f"{name}.tsv").write_text(trace)
    if reference:
        other, _ = execute(replay, reference=True)
        require(trace == other, first_difference(trace, other) +
                f"\nReproduce: python3 scripts/twin/replay.py {path} --reference --expect {evidence / (name + '.tsv')}")
    return trace, metrics


def check_cases(evidence):
    results = {}
    visibility = json.loads((HERE / "fixtures/visibility.json").read_text())
    trace, results["visibility"] = run(visibility, evidence, "visibility")
    states = rows(trace, "state")
    require(any(int(row[0]) < 295000 and row[6] == "1" for row in states), "boop never became active")
    require(any(int(row[0]) >= 295000 and row[5:7] == ["0", "1"] for row in states), "visor hid no active boop")
    require(any(500000 <= int(row[0]) < 600000 and row[5:7] == ["0", "0"] for row in states), "hidden boop did not expire across wrap")
    shutdown = [(int(r[0]), r[4]) for r in rows(trace, "spi") if r[3].startswith("0c")]
    require([value for at, value in shutdown if at > 200000] == ["1", "0", "1", "0"],
            "expected hidden, reopen, sleep, wake shutdown sequence")
    repeated, _ = execute(visibility)
    require(trace == repeated, "same replay and policy must repeat byte for byte")

    golden = {r[0]: r for r in (r.split("\t") for r in
              (ROOT / "scripts/tests/fixtures/rendering-v1.tsv").read_text().splitlines()[2:])}
    trace, results["expressions"] = run(scenario([
        [1000, "usb", "SET natural_blinks=0"],
        [100000, "usb", "EXPR 1"], [114000, "usb", "EXPR 2"], [119000, "usb", "EXPR 0"],
        [320000, "usb", "EXPR 1"], [520000, "usb", "EXPR 2"], [720000, "usb", "EXPR 3"],
        [920000, "usb", "EXPR 4"], [1120000, "usb", "EXPR 5"],
    ], 1320000), evidence, "expressions")
    names = ["happy", "surprised", "angry", "vwv", "sleepy", "wink"]
    physical = bytearray(112)
    for index, name in enumerate(names):
        at = 300000 + index * 200000
        actual = [row for row in rows(trace, "state") if int(row[0]) <= at][-1]
        require(actual[-1] == golden[f"rest.{name}"][-2], f"logical P1 mismatch for {name}")
        # Decode every wire transaction in order through each checkpoint.
        for row in rows(trace, "spi"):
            if int(row[0]) > at:
                break
            data = bytes.fromhex(row[3])
            require(len(data) == 28 and len(set(data[::2])) == 1, "incomplete SPI chain")
            opcode = data[0]
            if 1 <= opcode <= 8:
                for tile, value in enumerate(reversed(data[1::2])):
                    physical[tile * 8 + opcode - 1] = value
        require(physical.hex() == golden[f"rest.{name}"][-1], f"physical P1 mismatch for {name}")

    trace, results["queues"] = run(scenario([
        [120000, "hello", 1], [240000, "expression", 2], [260000, "duplicate", 0],
        [275000, "audio", 180], [280000, "hold", 450000], [300000, "expression", 3],
        [760000, "short-write", 1], [770000, "hold", 140000], [775000, "uart", "01" * 600 + "00"],
        [930000, "uart", "01" * 4], [1100000, "uart", "00"], [1110000, "sensor-stall", 1],
        [1120000, "audio-stall", 1], [1280000, "audio-stall", 0], [1300000, "sensor-stall", 0],
        [1350000, "disconnect", 1], [1400000, "disconnect", 0], [1530000, "hello", 2],
        [1680000, "expression", 5],
    ], 1840000, audio_tape=[510, 640, 510, 380] * 64, cosmetic_tape=[42, 0]), evidence, "queues")
    acks = [bytes.fromhex(r[-1])[-1] for r in rows(trace, "uart-packet") if r[3] == "3"]
    require(all(code in acks for code in (0, 1, 3)), f"missing accepted, duplicate or expired ACK: {acks}")
    m = results["queues"]
    require(m["audio_ring_peak"] == 511 and m["audio_dropped"] > 0, "audio overflow was not visible")
    require(m["uart_rx_peak"] == 256 and m["uart_wire_in_peak"] == 512 and
            m["uart_rx_dropped"] > 0 and m["uart_wire_dropped"] > 0, "UART overflow was not visible")
    require(m["skipped_loop_opportunities"] >= 500, "acceleration was not exercised")
    require(any(int(r[8]) for r in rows(trace, "state")), "sensor fault was not observable")
    require(state(trace)[3] == "5", "reconnected generation did not accept a command")

    slot = (ROOT / "scripts/tests/fixtures/eeprom-v1.hex").read_text().splitlines()[2]
    initial = slot + "ff" * 64
    trace, results["power_cut"] = run(scenario([
        [1000, "usb", "SET brightness=0"], [10000, "power-cut-after", 8], [12000, "usb", "SAVE"],
    ], 100000, eeprom=initial), evidence, "power-cut")
    require(len(rows(trace, "power-loss")) == 1, "expected a simulated power interruption")
    image = rows(trace, "eeprom")[0][-1]
    require(image[:128] == slot and image != initial, "interruption did not preserve previous slot and partial write")
    trace, results["restart"] = run(scenario([], 10000, epoch=2, eeprom=image), evidence, "restart")
    require(state(trace)[3] == "4" and state(trace)[13:15] == ["15", "1"], "reboot lost previous startup expression or approval")

    for field, bad in [("version", True), ("events", [[2000, "boop", 0], [1000, "boop", 0]]),
                       ("events", [[1000, "usb", "GET\nSAVE"]]), ("eeprom", "xyz"), ("loop_us", 0)]:
        invalid = copy.deepcopy(visibility)
        invalid[field] = bad
        try:
            encode(invalid)
        except ValueError:
            continue
        raise AssertionError(f"accepted malformed {field}")
    return results


def deep_campaign(evidence):
    results = {}
    for seed in range(16):
        rng = random.Random(seed)
        events = []
        for start in range(100000, 12_000_000, 500_000):
            events.extend([[start, "audio", rng.randrange(512)], [start, "boop", rng.randrange(1024)],
                           [start + 1000, "visor", rng.randrange(2)], [start + 1500, "hold", rng.randrange(1, 180000)],
                           [start + 200000, "uart", "01" * rng.randrange(1, 660) + "00"]])
        name = f"seed-{seed:02}"
        _, results[name] = run(scenario(events, 12_000_000, seed=seed), evidence, name)
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deep", action="store_true")
    parser.add_argument("--evidence", type=Path, default=ROOT / "build/twin/evidence")
    args = parser.parse_args()
    args.evidence.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    try:
        results = check_cases(args.evidence)
        if args.deep:
            results.update(deep_campaign(args.evidence))
    except (ValueError, AssertionError) as error:
        print(f"FAIL: {error}\nReplays and received traces: {args.evidence}", file=sys.stderr)
        return 1
    report = dict(cases=results, virtual_seconds=sum(m["virtual_us"] for m in results.values()) / 1e6,
                  wall_seconds=time.monotonic() - start)
    (args.evidence / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print(f"PASS: {len(results)} scenarios, {report['virtual_seconds']:.3f} virtual seconds; evidence: {args.evidence}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
