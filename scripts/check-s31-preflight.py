#!/usr/bin/env python3
"""Build-only ESP32-S31 preview qualification; never edits/uploads firmware.

Requires the existing pinned ESP-IDF/ML-KEM downloads. This is not a production
S31 build entry point or Protocol v4 implementation. Explicit --preview is
required because Espressif lists v6.1 S31 support as preview.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/s31-probe"
PACKAGES = ROOT / "build/platformio/packages"
FRAMEWORK = PACKAGES / "framework-espidf"
PYTHON_ENV = ROOT / "build/platformio/penv/.espidf-6.1.0"
CC = PACKAGES / "toolchain-riscv32-esp/bin/riscv32-esp-elf-gcc"
CXX = CC.with_name("riscv32-esp-elf-g++")
CMAKE = PACKAGES / "tool-cmake/bin/cmake"
NINJA = PACKAGES / "tool-ninja/ninja"
SIZE = CC.with_name("riscv32-esp-elf-size")
NM = CC.with_name("riscv32-esp-elf-nm")
TESTS = ROOT / "scripts/tests/pq-worker"
STACK = 49152


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args, log, cwd=ROOT):
    result = subprocess.run(list(map(str, args)), cwd=cwd, env=ENV,
                            capture_output=True, text=True, timeout=900)
    output = result.stdout + result.stderr
    log.write_text(output)
    if result.returncode:
        raise RuntimeError(f"{Path(str(args[0])).name} failed; inspect {log}")
    return output


def production_hashes():
    tracked = subprocess.check_output(["git", "ls-files", "firmware", "toolchain.lock.json"],
                                      cwd=ROOT, text=True).splitlines()
    return {name: digest(ROOT / name) for name in tracked if (ROOT / name).is_file()}


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def sections(elf):
    text = run([SIZE, "-A", elf], elf.with_suffix(".size.log"))
    sizes = {cols[0]: int(cols[1]) for line in text.splitlines()
             if len(cols := line.split()) == 3 and cols[0].startswith(".")}
    text = run([NM, "-n", elf], elf.with_suffix(".symbols.log"))
    symbols = {cols[2]: int(cols[0], 16) for line in text.splitlines()
               if len(cols := line.split()) == 3 and cols[2] == "_heap_start"}
    # Actual v6.1 S31 linker-region boundary, not total physical SRAM/host RSS.
    return {"sections": sizes, "symbols": symbols,
            "static_data_bss_bytes": sizes[".dram0.data"] + sizes[".dram0.bss"],
            "contiguous_linker_tail_before_runtime_allocations_bytes":
                0x2F07AFC0 - symbols["_heap_start"], "elf_sha256": digest(elf)}


def compiler_flags(entry):
    args = shlex.split(entry["command"])
    for option in ("-o", "-c"):
        pos = args.index(option)
        del args[pos:pos + 2]
    return args


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preview", action="store_true",
                        help="explicitly evaluate public S31 preview, not approve production")
    options = parser.parse_args()
    if not options.preview:
        parser.error("S31 v6.1 is preview; use --preview for isolated research only")
    BUILD.mkdir(parents=True, exist_ok=True)
    before = production_hashes()
    sdk_pins = json.loads((ROOT / "scripts/tests/s31-preflight-pins.json").read_text())
    for name, sha in sdk_pins["source_sha256"].items():
        if digest(FRAMEWORK / name) != sha:
            raise RuntimeError("SDK source hash mismatch: " + name)
    helper = module("pq_worker_preflight", ROOT / "scripts/check-pq-worker.py")
    pre = helper.pre
    source = helper.SOURCE
    pre.fetch(pre.PINS["archive"]["url"], pre.BUILD / "upstream.tar.gz",
              pre.PINS["archive"]["sha256"])
    for name, sha in pre.PINS["source_sha256"].items():
        if digest(source / name) != sha:
            raise RuntimeError("ML-KEM source hash mismatch: " + name)
    constraints = ROOT / "build/idf-tools/espidf.constraints.v6.1.txt"
    if not constraints.is_file():
        raise RuntimeError("Install ESP-IDF core Python dependencies and constraints first")
    global ENV
    ENV = dict(os.environ, IDF_PATH=str(FRAMEWORK), IDF_COMPONENT_MANAGER="0",
               IDF_TOOLS_PATH=str(constraints.parent), IDF_PYTHON_ENV_PATH=str(PYTHON_ENV),
               ESP_ROM_ELF_DIR=str(PACKAGES / "tool-esp-rom-elfs"))
    ENV["PATH"] = os.pathsep.join(map(str, [CC.parent, CMAKE.parent, NINJA.parent,
                                           PYTHON_ENV / "bin"])) + os.pathsep + ENV["PATH"]
    project = BUILD / "project"
    project.mkdir(exist_ok=True)
    (project / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.16)\n'
        f'set(EXTRA_COMPONENT_DIRS "{ROOT / "firmware/esp32/src"}")\n'
        'include($ENV{IDF_PATH}/tools/cmake/project.cmake)\n'
        'if(PROTOGEN_PROBE_ROLE STREQUAL "paw")\n'
        '  add_compile_definitions(PAW_CONTROLLER)\n'
        'else()\n  add_compile_definitions(HELMET_RECEIVER)\nendif()\n'
        'project(protogen_s31_baseline_probe)\n')
    defaults = (ROOT / "firmware/esp32/sdkconfig.defaults").read_text()
    (project / "sdkconfig.defaults").write_text(defaults.replace('"esp32c3"', '"esp32s31"'))
    report = {"scope": "isolated public S31 preview research; unchanged production v3",
              "measured_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "sdk_support": "PREVIEW; official supported tag v6.1.1 not published at assessment",
              "target": "esp32s31", "esp_idf": "6.1.0",
              "compiler": run([CC, "--version"], BUILD / "compiler.log").splitlines()[0],
              "mlkem_commit": pre.PINS["commit"], "sdk_pins": sdk_pins, "roles": {}}
    for role in ("helmet", "paw"):
        directory = BUILD / role
        config_log = BUILD / f"{role}-configure.log"
        run([CMAKE, "-S", project, "-B", directory, "-G", "Ninja",
             "-DIDF_TARGET=esp32s31", f"-DPROTOGEN_PROBE_ROLE={role}",
             f"-DPYTHON={PYTHON_ENV / 'bin/python'}"], config_log)
        build_log = BUILD / f"{role}-build.log"
        output = run([CMAKE, "--build", directory, "-j8"], build_log)
        if "warning:" in output.lower():
            raise RuntimeError("Compiler warnings in " + str(build_log))
        sdk = json.loads((directory / "config/sdkconfig.json").read_text())
        for key in ("SECURE_BOOT", "SECURE_FLASH_ENC_ENABLED", "NVS_ENCRYPTION",
                    "PROTOGEN_PRODUCTION", "ESP_WIFI_SOFTAP_SUPPORT"):
            if sdk.get(key, False):
                raise RuntimeError("Unexpected irreversible/security setting: " + key)
        if sdk["IDF_TARGET"] != "esp32s31" or sdk["ESP_MAIN_TASK_STACK_SIZE"] != 12288:
            raise RuntimeError("Unexpected target or main stack")
        elf = directory / "protogen_s31_baseline_probe.elf"
        baseline = sections(elf)
        baseline["binary_bytes"] = elf.with_suffix(".bin").stat().st_size
        baseline["binary_sha256"] = digest(elf.with_suffix(".bin"))
        entry = next(e for e in json.loads((directory / "compile_commands.json").read_text())
                     if e["file"].endswith("/main.cpp"))
        flags = compiler_flags(entry)
        extra = ["-Wall", "-Wextra", "-Werror", "-fstack-usage", "-fcallgraph-info=su",
                 "-I" + str(TESTS), "-I" + str(source / "mlkem"),
                 '-DMLK_CONFIG_FILE="native_config.h"']
        native = BUILD / f"{role}-native.o"
        run([CC, "@" + str(directory / "toolchain/cflags"), "-Og", "-fno-shrink-wrap",
             "-ffunction-sections", "-fdata-sections", "-fstrict-volatile-bitfields",
             "-fno-jump-tables", "-fno-tree-switch-conversion"] + extra +
            ["-c", source / "mlkem/mlkem_native.c", "-o", native], BUILD / f"{role}-native.log", directory)
        native_record = helper.object_record(native)
        native_record["known_call_paths"] = helper.call_paths([native.with_suffix(".ci")])
        kem = BUILD / f"{role}-kem.o"
        probe = BUILD / f"{role}-worker.o"
        run(flags + extra + ["-c", TESTS / "kem.cpp", "-o", kem], BUILD / f"{role}-kem.log", directory)
        run(flags + extra + [f"-DPQ_STACK_BYTES={STACK}", "-c", TESTS / "freertos_probe.cpp", "-o", probe],
            BUILD / f"{role}-worker.log", directory)
        link = run([NINJA, "-C", directory, "-t", "commands", elf.name],
                   BUILD / f"{role}-commands.log").splitlines()[-1]
        args = shlex.split(link.strip().removeprefix(": && ").split(" && ")[0])
        candidate = BUILD / f"{role}-worker.elf"
        args[args.index("-o") + 1] = str(candidate)
        args = ["-Wl,--Map=" + str(candidate.with_suffix(".map")) if a.startswith("-Wl,--Map=") else a
                for a in args]
        args += list(map(str, [probe, kem, native])) + [
            "-Wl,-u,pq_qualification_probe", "-Wl,-u,pq_qualification_submit",
            "-Wl,-u,pq_qualification_cancel", "-Wl,-u,pq_qualification_take"]
        run(args, BUILD / f"{role}-worker-link.log", directory)
        candidate_record = sections(candidate)
        candidate_record["static_ram_increase_bytes"] = (
            candidate_record["static_data_bss_bytes"] - baseline["static_data_bss_bytes"])
        report["roles"][role] = {"baseline": baseline, "worker_relink": candidate_record,
            "native": native_record, "worker": helper.object_record(probe),
            "adapter": helper.object_record(kem),
            "sdk_stacks": {k: v for k, v in sdk.items() if "STACK" in k and isinstance(v, int)},
            "cpu_mhz": sdk["ESP_DEFAULT_CPU_FREQ_MHZ"], "compiler_warnings": 0,
            "cmake_warning_count": config_log.read_text().count("CMake Warning")}
    report["worker_stack_bytes"] = STACK
    report["physical_hp_sram_bytes"] = 524288
    report["application_linker_sram_bytes"] = 503744
    report["mcu_runtime"] = {name: None for name in ("stack_high_water", "heap_minimum",
        "largest_free_block", "keygen_us", "encaps_us", "decaps_us", "watchdog",
        "uart_latency", "radio_latency", "power", "rf")}
    if production_hashes() != before:
        raise RuntimeError("Production sources changed during isolated qualification")
    for name, sha in sdk_pins["source_sha256"].items():
        if digest(FRAMEWORK / name) != sha:
            raise RuntimeError("SDK source changed during qualification: " + name)
    report["production_files_unchanged_sha256"] = before
    report["qualification_source_sha256"] = {
        str(p.relative_to(ROOT)): digest(p) for p in [Path(__file__),
            ROOT / "scripts/tests/s31-preflight-pins.json", *sorted(TESTS.glob("*"))] if p.is_file()}
    (BUILD / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS isolated S31 preview builds/static relinks; no production approval")


if __name__ == "__main__":
    main()
