#!/usr/bin/env python3
"""Prove focused host tests reject isolated, intentional changes to invariants."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from build_support import git_identity, source_identity, write_json

ROOT = Path(__file__).resolve().parents[1]
TEENSY = Path("firmware/PROT_OS7_MAX7219")
ESP = Path("firmware/esp32")
CONFIG = TEENSY / "Configuration.h"
STORE = TEENSY / "ConfigStore.h"
PROTOCOL = TEENSY / "src/HelmetProtocol/HelmetProtocol.h"
EFFICIENCY = TEENSY / "src/HelmetProtocol/Efficiency.h"
SESSION = ESP / "src/WirelessSession.cpp"

# Each replacement is checked to match exactly once and restored before the
# next case. The rejection must come from the selected assertion or diagnostic.
MUTATIONS = (
    ("brightness-approval", "config", CONFIG,
     "brightness < brightnessApproval ? brightness : brightnessApproval", "brightness",
     "unapproved.effectiveBrightness() == 0"),
    ("brightness-ceiling", "config", TEENSY / "Settings.h",
     "return requested > static_cast<unsigned>(MaxBrightness)\n"
     "      ? static_cast<uint8_t>(MaxBrightness) : static_cast<uint8_t>(requested);",
     "return static_cast<uint8_t>(requested);", "oversized.effectiveBrightness() == 1"),
    ("eeprom-crc", "config", STORE,
     "readNumber(data + crcAt, 4) != crc(data, crcAt)", "false", "sameConfig(actual, expected)"),
    ("eeprom-marker", "config", STORE, "data[markerAt] != 0xa5", "(static_cast<void>(data[markerAt]), false)",
     "sameConfig(actual, expected)"),
    ("command-freshness", "protocol", PROTOCOL,
     "if(!fresh(p.ticket,now))return fail(Expired);", "", "g.check(c,401)==Expired"),
    ("command-duplicate", "protocol", PROTOCOL,
     "?Duplicate:fail(Invalid);", "?Accepted:fail(Invalid);", "g.check(c,1000)==Duplicate"),
    ("invalid-efficiency-flag", "invalid", EFFICIENCY,
     "(PROTOGEN_MAIN_EVENTS == 0 || PROTOGEN_MAIN_EVENTS == 1)", "true",
     "efficiency-invalid: compile must fail"),
    ("replay-before-validation", "wireless", SESSION,
     "  replay.accept(seq);\n  p = decoded;", "  p = decoded;",
     "authenticatedAfterReject"),
    ("stale-animation-generation", "animation", TEENSY / "src/BehaviorCore/AnimationV2.cpp",
     " || tracks[handle.slot].generation != handle.generation", "", "!table.cancel(first.handle)"),
    ("animation-capacity", "animation", TEENSY / "src/BehaviorCore/AnimationV2.cpp",
     "if (active(slot) || track.generation == UINT32_MAX)", "if (track.generation == UINT32_MAX)",
     "second.handle.slot == 1"),
    ("timeline-reserved-field", "timeline", TEENSY / "src/BehaviorCore/Timeline.h",
     " || data[14] || data[15]", " || data[15]", "!validAsset(asset, sizeof(asset))"),
    ("supervisor-inactivity", "supervision", TEENSY / "Supervision.h",
     "if (!needed) {", "if (!needed && !required) {", "!s.foreground.required"),
)


def run(command, env, check=True):
    result = subprocess.run(command, cwd=ROOT, env=env, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=600)
    if check and result.returncode:
        raise RuntimeError(f"{command[0]} exited {result.returncode}\n{result.stdout[-8000:]}")
    return result


def isolated_source(directory):
    for path in ("scripts/tests", str(TEENSY), str(ESP / "src"), str(ESP / "protocol_v4")):
        shutil.copytree(ROOT / path, directory / path, ignore=shutil.ignore_patterns("__pycache__"))
    return directory


def exercise(directory, test, env):
    source = directory / TEENSY
    scripts = directory / "scripts/tests"
    program = directory / "probe"
    if test == "invalid":
        return run([sys.executable, str(ROOT / "scripts/check-invalid-configurations.py"),
                    "--firmware", str(directory / "firmware")], env, check=False)
    if test == "wireless":
        build = directory / "build"
        if not (build / "CMakeCache.txt").is_file():
            mbed = env.get("MBEDTLS_SOURCE", str(ROOT / "build/platformio/packages/framework-espidf/components/mbedtls/mbedtls"))
            run([env["CMAKE"], "-S", str(scripts / "wireless"), "-B", str(build),
                 "-DMBEDTLS_SOURCE=" + mbed, "-DHOST_SANITIZERS=" + env["PROTOGEN_HOST_SANITIZERS"],
                 "-DCMAKE_BUILD_TYPE=Debug"], env)
        # Same-second edits can otherwise reuse the control object on make-based
        # hosts. Recompile the sole mutated translation unit in the fixture.
        for object_file in (build / "CMakeFiles/test-wireless.dir").rglob("WirelessSession.cpp.o"):
            object_file.unlink()
        executable = build / "test-wireless"
        executable.unlink(missing_ok=True)
        compiled = run([env["CMAKE"], "--build", str(build), "--target", "test-wireless", "-j", "4"], env)
        if "WirelessSession.cpp" not in compiled.stdout or not executable.is_file():
            raise RuntimeError("Wireless mutation fixture failed to rebuild and link")
        return run([str(executable), "--session-only"], env, check=False)
    flags = [env["CXX"], "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=" + env["PROTOGEN_HOST_SANITIZERS"], "-fno-sanitize-recover=all",
             f"-I{scripts / 'stubs'}", f"-I{source}", f"-I{source / 'src/HelmetProtocol'}",
             f"-I{directory / ESP / 'src'}"]
    if test == "config":
        run([*flags, str(scripts / "config-compatibility.cpp"), "-o", str(program)], env)
        return run([str(program), str(scripts / "fixtures/eeprom-v1.hex")], env, check=False)
    if test in ("animation", "timeline", "supervision"):
        core = source / "src/BehaviorCore"
        if test == "animation":
            extra = ["-DPROTOGEN_ANIMATION_V2=1", "-I" + str(core),
                     str(scripts / "animation-v2.cpp"), str(core / "AnimationV2.cpp"), str(core / "BehaviorCore.cpp")]
        elif test == "timeline":
            extra = ["-DPROTOGEN_ANIMATION_V2=1", "-DPROTOGEN_ANIMATION_TIMELINES=1",
                     "-I" + str(core), str(scripts / "p8-timeline-format.cpp")]
        else:
            extra = [str(scripts / "supervision-unit.cpp")]
        run([*flags, *extra, "-o", str(program)], env)
        return run([str(program)], env, check=False)
    run([*flags, "-DHELMET_RECEIVER", str(scripts / "helmet-protocol.cpp"), "-o", str(program)], env)
    return run([str(program)], env, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", choices=("fast", "full", "wireless", "p8", "extended"), default="full")
    args = parser.parse_args()
    env = dict(os.environ)
    for key, value in (("CMAKE", "cmake"), ("CC", "clang"), ("CXX", "clang++"),
                       ("PROTOGEN_HOST_SANITIZERS", "address,undefined")):
        env.setdefault(key, value)
    output = ROOT / "build/mutation-evidence"
    output.mkdir(parents=True, exist_ok=True)
    manifest = output / (args.group + ".json")
    new_domains = {"animation", "timeline", "supervision"}
    domains = {"fast": {"config", "protocol", "invalid"}, "wireless": {"wireless"},
               "p8": new_domains, "extended": new_domains | {"wireless"}}
    selected = [entry for entry in MUTATIONS if args.group == "full" or entry[1] in domains[args.group]]
    record = {"schema": 1, **git_identity(ROOT), "source": source_identity(ROOT),
              "group": args.group, "results": [], "equivalent": [],
              "excluded": [name for name, *_ in MUTATIONS if name not in {row[0] for row in selected}]}
    write_json(manifest, record)
    with tempfile.TemporaryDirectory(prefix="protogen-mutations-") as tmp:
        directory = isolated_source(Path(tmp))
        controls = set()
        for name, test, relative, original, replacement, expected in selected:
            if test not in controls:
                control = exercise(directory, test, env)
                (output / f"{test}-control.log").write_text(control.stdout)
                if control.returncode:
                    raise RuntimeError(f"{test}: clean source failed; see {output}")
                controls.add(test)
            path = directory / relative
            text = path.read_text()
            if text.count(original) != 1:
                raise RuntimeError(f"{name}: expected one mutation point in {relative}")
            mutated = text.replace(original, replacement)
            if test == "wireless":
                marker = "  bool okay = Crypto::gcm(false,"
                if mutated.count(marker) != 1:
                    raise RuntimeError("Replay mutation insertion point changed")
                mutated = mutated.replace(marker, "  replay.accept(seq);\n" + marker)
            path.write_text(mutated)
            try:
                outcome = exercise(directory, test, env)
            finally:
                path.write_text(text)
            (output / f"{name}.log").write_text(outcome.stdout)
            killed = outcome.returncode != 0 and expected in outcome.stdout
            record["results"].append({"mutation": name, "test": test, "exit_code": outcome.returncode,
                                      "expected_failure": expected, "detected": killed,
                                      "classification": "killed" if killed else "survived"})
            write_json(manifest, record)
            print(f"{'PASS' if killed else 'FAIL'}: {name}", flush=True)
    record["inputs_unchanged"] = source_identity(ROOT) == record["source"]
    write_json(manifest, record)
    return 0 if record["inputs_unchanged"] and all(row["detected"] for row in record["results"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
