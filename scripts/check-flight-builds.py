#!/usr/bin/env python3
"""Build equivalent disabled/enabled target images and verify linker budgets."""
import argparse
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys

from build_support import BuildRun, check_toolchain, find_cli, write_json

ROOT = Path(__file__).resolve().parents[1]
FLAG = "-DPROTOGEN_FLIGHT_RECORDER=1"


def recorder_symbols(tool, elf):
    output = subprocess.check_output([str(tool), "-S", "-C", "--defined-only", str(elf)], text=True)
    return {name: {"address": int(address, 16), "bytes": int(size, 16), "type": kind}
            for address, size, kind, name in re.findall(r"^(\w+) (\w+) (\w) (p3(?:Teensy|C3)\w+|P3Teensy::[^\n]+)$", output, re.M)}


def deltas(disabled, enabled):
    return {name: value - disabled[name] for name, value in enabled.items() if name in disabled}


def teensy(evidence):
    feature_checks = runpy.run_path(str(ROOT / "scripts/check-features.py"))
    run = BuildRun(ROOT, "flight-recorder")
    sketch = run.snapshot()
    main = sketch / "PROT_OS7_MAX7219.ino"
    features = feature_checks["ALL_FEATURES"]
    main.write_text(feature_checks["configure_features"](main.read_text(), features, features))
    cli = find_cli()
    run.record["toolchain"] = check_toolchain(ROOT, cli)
    for enabled in (False, True):
        flags = "-Werror" + (" " + FLAG if enabled else "")
        artifact = run.compile(cli, sketch, features, extra_flags=flags, jobs=4)
        item = run.record["results"][-1]
        if item["exit_code"] or item.get("dependency_error") or item["warnings"]:
            run.finish("Target compilation failed")
            raise RuntimeError(f"Teensy compile failed; see {run.path}")
        objects = artifact / "objects"
        units = json.loads((objects / "compile_commands.json").read_text())
        nm = Path(units[0]["arguments"][0]).with_name("arm-none-eabi-nm")
        elf = objects / "PROT_OS7_MAX7219.ino.elf"
        symbols = recorder_symbols(nm, elf)
        if enabled:
            array = symbols["p3TeensyRecords"]
            if not (0x20200000 <= array["address"] < array["address"] + array["bytes"] <= 0x20280000):
                raise RuntimeError("Recorder is outside Teensy RAM2 (OCRAM)")
            if array["bytes"] != 7680 or item["memory"]["ram2_variables_bytes"] - run.record["results"][0]["memory"]["ram2_variables_bytes"] > 8192:
                raise RuntimeError("Teensy recorder exceeds RAM2 budget")
        elif symbols:
            raise RuntimeError("Disabled Teensy build unexpectedly contains recorder symbols")
        item.update(enabled=enabled, symbols=symbols)
        run.publish()
    if not run.finish():
        raise RuntimeError(f"Teensy verification failed; see {run.path}")
    report = {"cases": run.record["results"], "delta_bytes": deltas(*(r["memory"] for r in run.record["results"])),
              "run_manifest": str(run.path / "manifest.json")}
    write_json(evidence / "teensy.json", report)
    print(json.dumps(report["delta_bytes"]))


def c3(evidence):
    build = ROOT / "build"
    tool = build / "platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-nm"

    def compile_case(label, enabled):
        env = dict(os.environ, PROTOGEN_PIO_JOBS=os.getenv("PROTOGEN_PIO_JOBS", "4"))
        env.pop("PROTOGEN_UART_KEY_DIR", None)
        env["PLATFORMIO_BUILD_SRC_FLAGS"] = FLAG if enabled else ""
        result = subprocess.run([sys.executable, str(ROOT / "scripts/build-esp32.py")], env=env)
        report = json.loads((build / "esp32-build.json").read_text())
        report["recorder_enabled"] = enabled
        if "exit_code" in report and (build / "esp32-build.log").exists():
            shutil.copy2(build / "esp32-build.log", evidence / f"{label}.log")
        write_json(evidence / f"{label}.json", report)
        if result.returncode:
            raise RuntimeError(f"{label} failed")
        for role, target in report["targets"].items():
            symbols = recorder_symbols(tool, build / "esp32" / role / "firmware.elf")
            if enabled:
                if symbols["p3C3Records"]["bytes"] != 2048 or symbols["p3C3Records"]["type"] not in ("b", "B"):
                    raise RuntimeError(f"{role} ring missing from C3 .bss")
            elif symbols:
                raise RuntimeError(f"{role} disabled image contains recorder symbols")
            target["recorder_symbols"] = symbols
        write_json(evidence / f"{label}.json", report)
        return {role: target["memory"] for role, target in report["targets"].items()}

    first = compile_case("c3-disabled", False)
    sizes = {}
    try:
        second = compile_case("c3-enabled", True)
        for role in first:
            sizes[role] = {name: second[role][name]["used"] - first[role][name]["used"] for name in first[role]}
            increment = sizes[role]["ram_bytes"]
            if not 0 <= increment <= 4096:
                raise RuntimeError(f"{role} C3 static RAM increment is {increment} bytes (limit 4096)")
    finally:
        # Restore the standard image even if the opt-in build or guard fails.
        restored = compile_case("c3-restored", False)
        if first != restored:
            raise RuntimeError("restored C3 resource totals differ from disabled")
    write_json(evidence / "c3-deltas.json", sizes)
    print(json.dumps(sizes))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=("teensy", "c3"))
    args = parser.parse_args()
    out = ROOT / "build/flight-evidence"
    out.mkdir(parents=True, exist_ok=True)
    (teensy if args.target == "teensy" else c3)(out)
