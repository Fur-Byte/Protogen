#!/usr/bin/env python3
"""Isolated, pinned ML-KEM-1024 vector/resource preflight; never builds firmware.

The upstream deterministic RNG is linked ONLY into these host test executables.
No output of this script is a production key or firmware dependency.
"""
import collections
import ctypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/pq-preflight"
PINS = json.loads((ROOT / "scripts/tests/mlkem-preflight-pins.json").read_text())


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fetch(url, path, expected):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        with urllib.request.urlopen(url, timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != expected:
            raise RuntimeError(f"Download hash mismatch: {path.name}")
        path.write_bytes(data)
    if digest(path) != expected:
        raise RuntimeError(f"Cached input hash mismatch: {path.name}")


def run(argv, log=None, cwd=None):
    result = subprocess.run(argv, cwd=cwd, text=True, capture_output=True,
                            timeout=180)
    if log:
        log.write_text(result.stdout + result.stderr)
    if result.returncode:
        # Do not echo vector/key arguments, even though the fixtures are public.
        raise RuntimeError(f"{Path(argv[0]).name} failed; inspect {log}")
    return result.stdout


def vectors(binary):
    """Compare every ML-KEM-1024 case to NIST's exact expected output.

    Keep tcId/tgId and all ML-KEM-1024 cases; there are no skipped error cases.
    Byte-length checks are exercised by upstream's ACVP adapter, since the
    primitive itself has a fixed-buffer API without a length argument.
    """
    counts = collections.Counter()
    vector_dir = BUILD / "vectors"
    base = ("https://raw.githubusercontent.com/usnistgov/ACVP-Server/"
            + PINS["vectors"]["commit"] + "/gen-val/json-files/")
    for name, sha in PINS["vectors"]["files"].items():
        fetch(base + name, vector_dir / name, sha)
    for directory in sorted(vector_dir.iterdir()):
        prompt = json.loads((directory / "prompt.json").read_text())
        expected = json.loads((directory / "expectedResults.json").read_text())
        groups = {g["tgId"]: {t["tcId"]: t for t in g["tests"]}
                  for g in expected["testGroups"]}
        for group in prompt["testGroups"]:
            if group["parameterSet"] != "ML-KEM-1024":
                continue
            function = group.get("function", "keyGen")
            for case in group["tests"]:
                args = [str(binary), prompt["mode"], group["testType"]]
                if function == "keyGen":
                    args += ["z=" + case["z"], "d=" + case["d"]]
                else:
                    args.append(function)
                    if function == "encapsulation":
                        args += ["ek=" + case["ek"], "m=" + case["m"]]
                    elif function == "decapsulation":
                        args += (["seed=" + case["d"] + case["z"]]
                                 if group.get("keyFormat") == "seed"
                                 else ["dk=" + case["dk"]])
                        args.append("c=" + case["c"])
                    elif function == "encapsulationKeyCheck":
                        args.append("ek=" + case["ek"])
                    elif function == "decapsulationKeyCheck":
                        args.append("dk=" + case["dk"])
                    else:
                        raise RuntimeError(f"Unsupported vector function {function}")
                # Invalid-length fixtures can emit an expected diagnostic on
                # stderr; their testPassed result must still match NIST.
                output = run(args)
                actual = {"tcId": case["tcId"]}
                for line in output.splitlines():
                    key, sep, value = line.partition("=")
                    if sep and key.isidentifier():
                        actual[key] = bool(int(value)) if key == "testPassed" else value
                if actual != groups[group["tgId"]][case["tcId"]]:
                    raise RuntimeError(f"Vector mismatch: {directory.name}, "
                                       f"group {group['tgId']}, case {case['tcId']}")
                counts[f"{directory.name}/{function}"] += 1
    if sum(counts.values()) != 145:
        raise RuntimeError("Unexpected vector count; refusing a reduced test set")
    return dict(counts)


def benchmark(source):
    """Host-only primitive times including FFI overhead; not MCU estimates."""
    library = BUILD / "benchmark.so"
    run([os.environ.get("CC", "cc"), "-O2", "-std=c99", "-Wall", "-Wextra",
         "-Werror", "-fPIC", "-shared", "-DMLK_CONFIG_PARAMETER_SET=1024",
         "-DMLK_CONFIG_NO_RANDOMIZED_API",
         "-DMLK_CONFIG_NAMESPACE_PREFIX=protogen_preflight", "-Imlkem",
         "mlkem/mlkem_native.c", "-o", str(library)],
        BUILD / "benchmark-build.log", source)
    api = ctypes.CDLL(str(library))
    names = {"keygen": ("keypair_derand", 3), "encaps": ("enc_derand", 4),
             "decaps": ("dec", 3)}
    operations = {}
    for name, (symbol, arity) in names.items():
        function = getattr(api, "protogen_preflight_" + symbol)
        function.argtypes = [ctypes.POINTER(ctypes.c_ubyte)] * arity
        function.restype = ctypes.c_int
        operations[name] = function
    buffers = [ (ctypes.c_ubyte * n)() for n in (1568, 3168, 1568, 32, 32, 64, 32) ]
    pk, sk, ct, ss, recovered, coins, message = buffers
    samples = collections.defaultdict(list)
    cpu_start = time.process_time()
    try:
        for _ in range(64):
            ctypes.memmove(coins, os.urandom(64), 64)
            ctypes.memmove(message, os.urandom(32), 32)
            for name, args in [("keygen", (pk, sk, coins)),
                               ("encaps", (ct, ss, pk, message)),
                               ("decaps", (recovered, ct, sk))]:
                start = time.perf_counter()
                status = operations[name](*args)
                samples[name].append(1000 * (time.perf_counter() - start))
                if status:
                    raise RuntimeError(f"Host benchmark {name} failed")
            if bytes(ss) != bytes(recovered):
                raise RuntimeError("Host benchmark shared-secret mismatch")
    finally:
        # Explicit external writes for host-only buffers. Python/FFI copies
        # are not covered by a production zeroization guarantee.
        for buffer in buffers:
            ctypes.memset(ctypes.addressof(buffer), 0, ctypes.sizeof(buffer))
    return {"scope": "host portable C -O2; deterministic APIs with random test inputs; FFI included; no PCT; not MCU timing",
            "iterations": 64, "process_cpu_ms_total": 1000 * (time.process_time() - cpu_start),
            "operations_ms": {name: {"mean": sum(values) / len(values), "maximum": max(values)}
                              for name, values in samples.items()}}


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    archive = BUILD / "upstream.tar.gz"
    fetch(PINS["archive"]["url"], archive, PINS["archive"]["sha256"])
    source = BUILD / ("mlkem-native-" + PINS["commit"])
    if not source.exists():
        with tarfile.open(archive) as tar:
            tar.extractall(BUILD, filter="data")
    for name, sha in PINS["source_sha256"].items():
        if digest(source / name) != sha:
            raise RuntimeError(f"Upstream source hash mismatch: {name}")

    # Build the entire portable C implementation with both sanitizers, not
    # just a caller linked to an unsanitized library. Non-PIE avoids ASan
    # shadow-memory placement conflicts in constrained host environments.
    common = [os.environ.get("CC", "cc"), "-O2", "-std=c99", "-Wall",
              "-Wextra", "-Werror", "-fsanitize=address,undefined",
              "-fno-sanitize-recover=all", "-fno-pie", "-no-pie",
              "-DMLK_CONFIG_PARAMETER_SET=1024", "-Imlkem",
              "mlkem/mlkem_native.c", "test/notrandombytes/notrandombytes.c"]
    acvp = BUILD / "acvp-1024-sanitized"
    functional = BUILD / "functional-1024-sanitized"
    for binary, entry in [(acvp, "test/acvp/acvp_mlkem.c"),
                          (functional, "test/src/test_mlkem.c")]:
        run(common + [entry, "-o", str(binary)],
            BUILD / (binary.name + "-build.log"), source)
    counts = vectors(acvp)
    run([str(functional)], BUILD / "functional.log")
    host_times = benchmark(source)
    clang = os.environ.get("MLKEM_CLANG") or shutil.which("clang")
    if not clang:
        packaged = ROOT.parent / "toolchains/clang/usr/lib/llvm-19/bin/clang"
        if packaged.is_file():
            clang = str(packaged)
    analysis = "not available"
    if clang:
        log = BUILD / "static-analyzer.log"
        run([clang, "--analyze", "-Xanalyzer", "-analyzer-output=text",
             "-std=c99", "-Wall", "-Wextra", "-Werror",
             "-DMLK_CONFIG_PARAMETER_SET=1024", "-Imlkem",
             "mlkem/mlkem_native.c"], log, source)
        if log.read_text().strip():
            raise RuntimeError("Static Analyzer emitted diagnostics; inspect its log")
        analysis = run([clang, "--version"]).splitlines()[0] + "; no diagnostics"

    cross = Path(os.environ.get("MLKEM_CROSS_CC", str(ROOT /
        "build/platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-gcc")))
    size = cross.with_name("riscv32-esp-elf-size")
    compiler = run([str(cross), "--version"]).splitlines()[0]
    target_flags = ROOT / "build/esp32/helmet/toolchain/cflags"
    if not target_flags.is_file():
        raise RuntimeError("Run scripts/build-esp32.py before target resource preflight")
    resources = {}
    for level in (1024, 768):  # 768 is comparison only; no production substitution.
        for opt in ("-Og", "-Os"):
            name = f"mlkem{level}{opt}"
            obj = BUILD / (name + ".o")
            flags = ["@" + str(target_flags), opt, "-fno-shrink-wrap",
                     "-fstrict-volatile-bitfields", "-fno-jump-tables",
                     "-fno-tree-switch-conversion", "-Wall", "-Wextra", "-Werror",
                     "-fstack-usage", f"-DMLK_CONFIG_PARAMETER_SET={level}",
                     "-Imlkem"]
            run([str(cross)] + flags + ["-c", "mlkem/mlkem_native.c", "-o", str(obj)],
                BUILD / (name + "-build.log"), source)
            frames = {}
            for line in obj.with_suffix(".su").read_text().splitlines():
                label, amount, kind = line.split("\t")
                if kind != "static":
                    raise RuntimeError("Unbounded stack usage in candidate")
                frames[label.rsplit(":", 1)[1]] = int(amount)
            text, data, bss = map(int, run([str(size), str(obj)]).splitlines()[1].split()[:3])
            resources[name] = {"flags": flags, "frames_bytes": frames,
                               "object_text_bytes": text, "object_data_bytes": data,
                               "object_bss_bytes": bss}
    stacks = {}
    for role in ("helmet", "paw"):
        sdk = json.loads((ROOT / f"build/esp32/{role}/config/sdkconfig.json").read_text())
        stacks[role] = sdk["ESP_MAIN_TASK_STACK_SIZE"]
    baseline = json.loads((ROOT / "build/esp32-build.json").read_text())
    exceeds_stack = max(resources["mlkem1024-Og"]["frames_bytes"].values()) > min(stacks.values())
    gate = ("NO_GO_FOR_DIRECT_INTEGRATION_IN_CURRENT_MAIN_TASK" if exceeds_stack
            else "PENDING_COMPLETE_CALLCHAIN_AND_MCU_RESOURCE_QUALIFICATION")
    report = {"scope": "preflight; no protocol integration",
              "measured_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "implementation_commit": PINS["commit"], "vectors": counts,
              "vectors_total": sum(counts.values()),
              "sanitizers": "ASan+UBSan applied to full portable C implementation",
              "upstream_functional_iterations": 1000, "compiler": compiler,
              "host_primitive_benchmark": host_times,
              "static_analyzer": analysis,
              "sdk_target_cflags": target_flags.read_text(),
              "configured_task_stack_bytes": stacks,
              "resources": resources,
              "baseline_build_state": baseline["state"],
              "baseline_warnings": baseline.get("warnings"),
              "baseline_targets": baseline.get("targets"),
              "gate": gate,
              "limitations": ["Frames are compiler static estimates, not full call-chain high-water",
                              "Object text is not a linked firmware-size delta",
                              "No production RNG integration or PCT enabled in estimates",
                              "No whole-MCU impossibility conclusion; larger stack/allocation remains unqualified",
                              "MCU RAM/heap/stack peaks, timings, watchdog and radio tests still required"]}
    (BUILD / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: 145 NIST ML-KEM-1024 cases; 1000 upstream functional iterations; ASan/UBSan")
    print(gate)
    print("Evidence: build/pq-preflight/results.json")


if __name__ == "__main__":
    main()
