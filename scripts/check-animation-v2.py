#!/usr/bin/env python3
"""Run independently specified V2 visuals plus shared-sketch demonstrations."""
import argparse
import gzip
import json
from pathlib import Path
import random
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/twin"))
import replay
from test_replay import rows, scenario

FIXTURE = ROOT / "scripts/tests/fixtures/rendering-v1.tsv"
DEMO = ROOT / "scripts/twin/fixtures/animation-v2.json"
OUT = ROOT / "build/p5-evidence"
TRACKS = "python3 scripts/check-animation-v2.py"


def build(name, flags, goals):
    directory = ROOT / "build" / name
    subprocess.run(["make", "-C", str(ROOT / "scripts/twin"), "BUILD=../../build/" + name,
                    "EXTRA_FLAGS=" + flags, *goals], check=True)
    return directory


def frozen():
    return {r[0][5:]: bytes.fromhex(r[-2]) for r in (line.split("\t") for line in FIXTURE.read_text().splitlines())
            if r[0].startswith("rest.") and r[0] != "rest.unchanged"}


def reference(frame, *, left=(0, 0, 0), right=(0, 0, 0)):
    # Transform the archived eye masks by inverse destination lookup.
    output = bytearray(frame)
    for origin, (shift, vertical, cut) in ((96, left), (0, right)):
        for y in range(8):
            for x in range(16):
                old_x, old_y = x - shift, y - vertical
                index = y * 14 + (origin + x) // 8
                mask = 128 >> (x % 8)
                lit = (0 <= old_x < 16 and cut <= old_y < 8 and
                       frame[old_y * 14 + (origin + old_x) // 8] & (128 >> (old_x % 8)))
                output[index] = (output[index] & ~mask) | (mask if lit else 0)
    return bytes(output)


def at(trace, time):
    return next(r for r in reversed(rows(trace, "state")) if int(r[0]) <= time)


def visual_at(trace, time, expected, name, evidence):
    row = at(trace, time)
    actual = bytes.fromhex(row[-1])
    if actual == expected:
        return
    for i, (a, b) in enumerate(zip(expected, actual)):
        if a != b:
            bit = next(j for j in range(8) if ((a ^ b) & (128 >> j)))
            x, y = (i % 14) * 8 + bit, i // 14
            ids = [r[2:] for r in rows(trace, "v2-track") if int(r[0]) <= int(row[0])]
            raise ValueError(f"{name}: first divergent pixel ({x},{y}) at {row[0]} us, "
                             f"expected {bool(a & (128 >> bit))}, actual {bool(b & (128 >> bit))}; "
                             f"admission rows (input, status, slot, generation): {ids}; trace: {evidence}")


def compare(trace, ref):
    if trace != ref:
        raise ValueError(replay.first_difference(ref, trace))


def check_demo(trace, evidence):
    f = frozen()
    angled = reference(f["happy"], left=(-2, 0, 0), right=(-2, 0, 0))
    smile_right = reference(f["happy"], left=(2, 0, 0), right=(2, 0, 0))
    cases = [
        ("gaze starts left", 137000, angled),
        ("gaze crosses center", 228000, f["happy"]),
        ("gaze settles right", 328000, smile_right),
        ("left lid before right lid", 425000, reference(f["happy"], left=(0, 0, 8))),
        ("independent lid closure", 465000, reference(f["happy"], left=(0, 0, 8), right=(0, 0, 5))),
        ("boop wins over gaze", 1080000, f["vwv"]),
        ("gaze resumes after expiry", 1380000, reference(f["angry"], left=(-2, 0, 0), right=(-2, 0, 0))),
        ("same-time per-eye tie", 1500000, reference(f["angry"], left=(2, 0, 0), right=(-2, 0, 0))),
        ("wake after hidden track expiry", 1910000, f["angry"]),
    ]
    for name, time, expected in cases:
        visual_at(trace, time, expected, name, evidence)
    settle = reference(f["angry"], left=(0, 0, 3), right=(0, 0, 3))
    settling = [r for r in rows(trace, "state") if 760000 <= int(r[0]) < 920000 and bytes.fromhex(r[-1]) == settle]
    if not settling:
        raise ValueError(f"no independent settling pose after causal completion: {evidence}")
    quiet = [r for r in rows(trace, "spi") if 1670000 <= int(r[0]) < 1840000 and r[3][:2] in {f"{i:02x}" for i in range(1, 9)}]
    if quiet:
        raise ValueError(f"blanked display emitted digit writes: {quiet[0]}")
    usb = b"".join(bytes.fromhex(r[-1]) for r in rows(trace, "usb"))
    latency = next(d for line in usb.splitlines() if "feedback_valid" in (d := json.loads(line)))
    if not (latency["feedback_valid"] and latency["onset_valid"] and latency["completed"] == 1
            and latency["feedback_us"] <= latency["target_onset_us"] <= latency["completion_us"]):
        raise ValueError(f"invalid causal response after milliseconds wrap: {latency}")
    return cases + [("short settling squint", int(settling[0][0]), settle)]


def execute(scenario_input, path):
    trace, stats = replay.execute(scenario_input)
    path.write_text(trace)
    compare(trace, replay.execute(scenario_input, reference=True)[0])
    return trace, stats


def check_hidden(evidence):
    source = json.loads((ROOT / "scripts/twin/fixtures/visibility.json").read_text())
    source["events"].insert(1, [20000, "v2-track", [3, 0, 7, 0, 370, 2, 2]])
    trace, metrics = execute(source, evidence / "hidden-boop.tsv")
    if at(trace, 450000)[5:7] != ["0", "1"] or at(trace, 570000)[5:7] != ["0", "0"]:
        raise ValueError("boop did not expire while the visor was hidden")
    if any(310000 <= int(r[0]) < 735000 for r in rows(trace, "spi")):
        raise ValueError("hidden boop or track expiry emitted SPI")
    visual_at(trace, 790000, frozen()["happy"], "hidden track must stay expired", evidence / "hidden-boop.tsv")
    return metrics


def check_speech(evidence):
    case = scenario([
        [1000, "usb", "SET natural_blinks=0"],
        [80000, "v2-track", [2, 5, 7, 0, 430, 3, 3]],
        [90000, "v2-track", [1, 0, 7, 0, 430, 2, 2]],
        [100000, "audio", 300],
        [420000, "audio", 0],
    ], 620000)
    trace, metrics = execute(case, evidence / "speaking-gaze.tsv")
    speaking = [at(trace, t) for t in (180000, 260000, 400000)]
    right = reference(frozen()["happy"], right=(2, 0, 0))
    if any(r[7] != "1" for r in speaking) or metrics["audio_dropped"] or metrics["adc_samples"] < 1200:
        raise ValueError(f"speaking or acquisition regressed: {metrics}")
    before = bytes.fromhex(at(trace, 95000)[-1])
    for row in speaking:
        speaking_frame = bytes.fromhex(row[-1])
        if any(speaking_frame[y*14:y*14+2] != right[y*14:y*14+2] for y in range(8)):
            raise ValueError(f"speech and gaze did not compose at {row[0]} us")
    if not any(bytes.fromhex(row[-1])[y*14+2:y*14+6] != before[y*14+2:y*14+6]
               for row in speaking for y in range(8)):
        raise ValueError("animated mouth suppressed actual speech")
    return metrics


def check_causal_interaction(evidence):
    archive = ROOT / "scripts/tests/fixtures/behavior-p3.json.gz"
    cases = json.loads(gzip.decompress(archive.read_bytes()))["cases"]
    case = next(c["input"] for c in cases if c["name"] == "collision")
    trace, metrics = execute(case, evidence / "ordered-causal-recorder.tsv")
    usb = b"".join(bytes.fromhex(r[-1]) for r in rows(trace, "usb"))
    records = [d["record"] for line in usb.splitlines() if "record" in (d := json.loads(line))]
    admissions = [(r[3], r[4]) for r in records if r[1:3] == [243000, 2]]
    if admissions != [(0x103, 1), (0x202, 2), (0x003, 3)]:
        raise ValueError(f"P3 causal admission order or IDs differed: {admissions}")
    return metrics


def check_retarget_handoff(evidence):
    cases = {
        "retarget-settle": scenario([[1000, "usb", "SET natural_blinks=0"], [100000, "usb", "EXPR 2"],
                                    [300000, "usb", "EXPR 1"], [550000, "usb", "LATENCY"]], 600000),
        "retarget-override": scenario([[1000, "usb", "SET natural_blinks=0"],
                                      [100000, "v2-track", [3, 4, 7, 0, 800, 2, 2]],
                                      [200000, "usb", "EXPR 2"], [450000, "usb", "LATENCY"]], 500000),
    }
    report = {}
    for name, case in cases.items():
        path = evidence / (name + ".tsv")
        trace, metrics = execute(case, path)
        report[name] = metrics
        usb = b"".join(bytes.fromhex(r[-1]) for r in rows(trace, "usb"))
        latency = next(d for line in usb.splitlines() if "feedback_valid" in (d := json.loads(line)))
        if name == "retarget-settle":
            first = int(at(trace, 301000)[0])
            before = bytes.fromhex(at(trace, 299999)[-1])
            # A nonzero settling squint was already emitted. At elapsed zero,
            # a new command must not reopen it or record cosmetic feedback.
            if before == frozen()["angry"]:
                raise ValueError("reference retarget did not interrupt the settling pose")
            visual_at(trace, 301000, before, name, path)
            if not (latency["feedback_valid"] and latency["feedback_us"] >= 2000):
                raise ValueError(f"{name}: false elapsed-zero feedback after {first} us: {latency}")
        else:
            angry = frozen()["angry"]
            visual_at(trace, 199999, angry, "override emits requested art before admission", path)
            visual_at(trace, 201000, angry, name, path)
            if not latency["target_already_visible"] or latency["target_onset_us"] != 0:
                raise ValueError(f"{name}: already-visible target art was not retained: {latency}")
    return report


def stress(evidence):
    # Independent oracle for high-volume deterministic admission: a fixed
    # ordered stream is emitted once per policy and all outputs must agree.
    results = {}
    for seed in range(12):
        generator = random.Random(seed)
        events = [[1000, "usb", "SET natural_blinks=0"]]
        for time in range(20000, 4_000_000, 15000):
            region = generator.randrange(2)
            gaze = generator.randrange(-2, 3)
            events.append([time, "v2-track", [region, 0, generator.randrange(8), generator.randrange(5),
                                                generator.randrange(20, 220), gaze, -gaze]])
            if (time // 15000) % 9 == 0:
                events.extend([[time, "visor", generator.randrange(2)], [time, "hold", 3000]])
        trace, metrics = execute(scenario(events, 4_050_000, seed=seed), evidence / f"stress-{seed:02}.tsv")
        if metrics["inputs_processed"] != len(events) or len(rows(trace, "v2-track")) != sum(e[1] == "v2-track" for e in events):
            raise ValueError(f"seed {seed} lost an input event")
        results[str(seed)] = metrics
    return results


def build_demo(args):
    OUT.mkdir(parents=True, exist_ok=True)
    flags = "-DPROTOGEN_ANIMATION_V2=1"
    v2 = build("twin-v2", flags, ("all",))
    mixed = build("twin-v2-check", flags + " -DPROTOGEN_CACHE_FACE=1 -DPROTOGEN_FLIGHT_RECORDER=1 -DPROTOGEN_EARLY_EXPRESSION_REVEAL=1",
                  ("all", "animation-v2-tests"))
    unit = json.loads(subprocess.check_output([mixed / "animation-v2-unit"], text=True))
    pixels = json.loads(subprocess.check_output([mixed / "animation-v2-visual", FIXTURE], text=True))
    data = json.loads(DEMO.read_text())
    replay.PROGRAM = v2 / "twin"
    trace_file = OUT / "demo.tsv"
    trace, metrics = execute(data, trace_file)
    cases = check_demo(trace, trace_file)
    hidden = check_hidden(OUT)
    speech = check_speech(OUT)
    retarget = check_retarget_handoff(OUT)
    # Each build must reproduce its own trace. Early reveal can change frames
    # across builds, but track admission must match.
    replay.PROGRAM = mixed / "twin"
    interaction, interaction_stats = execute(data, OUT / "demo-interactions.tsv")
    if [r[3:] for r in rows(interaction, "v2-track")] != [r[3:] for r in rows(trace, "v2-track")]:
        raise ValueError("recorder/cache/early reveal changed track admission outcomes")
    causal = check_causal_interaction(OUT)
    replay.PROGRAM = v2 / "twin"
    seeded = stress(OUT) if args.deep else {}
    result = {"unit": unit, "visual": pixels, "demo": metrics, "hidden": hidden, "speech": speech, "causal": causal, "retarget": retarget,
              "interaction": interaction_stats, "stress": seeded}
    (OUT / ("deep-v2.json" if args.deep else "fast-v2.json")).write_text(json.dumps(result, indent=2) + "\n")
    if args.demo:
        for name, at_us, _ in cases:
            print(f"\n{name} · {at_us / 1000:g} ms")
            actual = bytes.fromhex(at(trace, at_us)[-1])
            for y in range(8):
                print("".join("#" if actual[y*14+x//8] & (128 >> (x%8)) else "." for x in range(112)))
    print(f"PASS: independent V2 visual/physical cases, track scheduling, demo and {len(seeded)} stress seeds")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deep", action="store_true")
    parser.add_argument("--demo", action="store_true")
    try:
        build_demo(parser.parse_args())
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"{error}\nReproduce: {TRACKS}\n")


if __name__ == "__main__":
    main()
