#!/usr/bin/env python3
"""Check the six Audio V2 combinations and capture isolated Teensy resource deltas."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from build_support import BuildRun, check_toolchain, find_cli, write_json
from sketch_features import ALL_FEATURES, configure_features
from timelines.compiler import compile_document

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/p7-evidence"
PROFILES = [(animation, timelines, audio, True)
            for animation, timelines in ((0, 0), (1, 0), (1, 1)) for audio in (0, 1)]
PROFILES += [(1, 1, 1, False)]
FUNCTIONS = re.compile(r"AudioV2::|processMicrophoneSample|resetMicrophoneDetector|\bfft\(|\brenderFrame\(|\bloop\(")


def profile_name(profile):
    return "".join(map(str, profile[:3])) + ("" if profile[3] else "-no-mic")


def defines(profile):
    animation, timelines, audio, _ = profile
    return (f"-DPROTOGEN_ANIMATION_V2={animation} -DPROTOGEN_ANIMATION_TIMELINES={timelines} "
            f"-DPROTOGEN_AUDIO_V2={audio}")


def host():
    asset = compile_document({
        "version": 1, "id": "audio-priority", "name": "Quiet mouth layer", "duration_ms": 5000,
        "initial": {"expression": 0, "natural_blinks": False, "idle_motion": False,
                    "cosmetic_seed": 19, "audio": 0},
        "events": [{"at_ms": 0, "duration_ms": 5000, "region": "mouth", "channel": "aperture",
                    "from": 3, "to": 3, "easing": "linear", "priority": 4}],
    })
    header = OUT / "AudioTestTimeline.h"
    if not header.exists() or header.read_text() != asset.header():
        header.write_text(asset.header())
    profiles = [(profile_name(p), p, "") for p in PROFILES]
    profiles.append(("111-recorder-cache-off", PROFILES[-2],
                     "-DPROTOGEN_FLIGHT_RECORDER=0 -DPROTOGEN_CACHE_FACE=0"))
    for name, profile, interactions in profiles:
        directory = f"build/audio-profile-{name}"
        flags = defines(profile) + " " + (interactions or
            "-DPROTOGEN_FLIGHT_RECORDER=1 -DPROTOGEN_CACHE_FACE=1 -DPROTOGEN_EARLY_EXPRESSION_REVEAL=1")
        targets = ["audio-profile"]
        if name == "111":
            targets += ["microphone-recovery", "audio-v2-unit"]
        command = ["make", "-C", "scripts/twin", "BUILD=../../" + directory,
                   "EXTRA_FLAGS=" + flags, "MICROPHONE=" + ("-DMicrophone" if profile[3] else ""),
                   "TIMELINE_HEADER=" + str(header), *targets]
        with (OUT / f"profile-{name}.log").open("w") as log:
            subprocess.run(command, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
            output = subprocess.check_output([str(ROOT / directory / "audio-profile")], text=True)
            log.write(output)
            if name == "111":
                for case in ("isolation", "permanent", "wrap", "rail", "invalid", "start", "stall",
                             "bounded", "disable"):
                    log.write(subprocess.check_output([str(ROOT / directory / "microphone-recovery"), case],
                                                      text=True))
        if name == "111":
            (OUT / "overload-demo.txt").write_text(output)
        print(f"PASS: host audio profile {name}", flush=True)


def target():
    run = BuildRun(ROOT, "audio-footprint")
    try:
        sketch = run.snapshot()
        source = sketch / "PROT_OS7_MAX7219.ino"
        original = source.read_text()
        cli = find_cli()
        run.record["toolchain"] = check_toolchain(ROOT, cli)
        for profile in PROFILES:
            name = profile_name(profile)
            flags = defines(profile)
            enabled = tuple(flag for flag in ALL_FEATURES if profile[3] or flag != "Microphone")
            source.write_text(configure_features(original, enabled, ALL_FEATURES))
            artifact = run.compile(cli, sketch, enabled, extra_flags=f"-Werror -fstack-usage {flags}", jobs=4)
            result = run.record["results"][-1]
            result["profile"], result["defines"] = name, flags
            if result["exit_code"] or result["warnings"] or result.get("dependency_error"):
                raise RuntimeError(f"{name}: target compile failed, see {run.log}")
            objects = artifact / "objects"
            db = json.loads((objects / "compile_commands.json").read_text())
            nm = Path(db[0]["arguments"][0]).with_name("arm-none-eabi-nm")
            elf = objects / "PROT_OS7_MAX7219.ino.elf"
            symbols = subprocess.check_output([str(nm), "-C", "-S", "--defined-only", str(elf)], text=True)
            state = re.search(r"^(\w+) (\w+) ([dbDB]) audioDetector$", symbols, re.M)
            expected = bool(profile[2] and profile[3])
            if bool(state) != expected or ("AudioV2::Detector::sample" in symbols) != expected:
                raise RuntimeError(f"{name}: unexpected detector linkage")
            result["detector_state_bytes"] = int(state[2], 16) if state else 0
            result["detector_address"] = state[1] if state else None
            result["stack_usage"] = [line for report in sorted(objects.rglob("*.su"))
                                     for line in report.read_text().splitlines() if FUNCTIONS.search(line)]
            run.publish()
            print(f"PASS: target audio profile {name} {result['memory']}", flush=True)
        if not run.finish():
            raise RuntimeError(f"target provenance validation failed, see {run.log}")
        write_json(OUT / "target.json", {"head": run.record["git_head"], "toolchain": run.record["toolchain"],
                   "working_tree_dirty": run.record["working_tree_dirty"], "cases": run.record["results"]})
    except Exception as error:
        run.finish(str(error))
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", action="store_true", help="compile pinned Teensy images instead of host profiles")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    (target if args.target else host)()


if __name__ == "__main__":
    main()
