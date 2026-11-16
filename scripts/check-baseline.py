#!/usr/bin/env python3
"""Run existing host suites and record their exact commands and outcomes."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

from build_support import git_identity, source_identity, write_json

ROOT = Path(__file__).resolve().parents[1]
SHELL_SUITES = (
    "firmware", "efficiency", "latency", "postmerge-rendering", "adc-sleep",
    "uart-admission", "ingress-peer", "postmerge-wireless", "s31-platform",
    "wireless", "v4", "dtls",
)
STANDARD_PYTHON_SUITES = ("host-support", "runner-portability", "board-targets", "scheduler-config")
OPTIMIZED_PYTHON_SUITES = (
    "toolchain-integrity", "tools", "security-builds", "helmet-tool", "pair-paw",
    "efficiency", "host-build",
)


def main():
    output = ROOT / "build/baseline-host"
    output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    for key, default in (("CC", "clang"), ("CXX", "clang++"), ("CMAKE", "cmake")):
        env.setdefault(key, default)
    captured = ("CC", "CXX", "CMAKE", "ARDUINO_FFT_SRC", "MBEDTLS_SOURCE",
                "PROTOGEN_HOST_SANITIZERS", "ASAN_OPTIONS")
    record = {"schema": 1, **git_identity(ROOT), "source": source_identity(ROOT),
              "environment_overrides": {key: env[key] for key in captured if key in env},
              "versions": {}, "results": []}
    for command in ([sys.executable, "--version"], [env["CXX"], "--version"],
                    [env["CMAKE"], "--version"], ["node", "--version"]):
        try:
            record["versions"][command[0]] = subprocess.check_output(
                command, text=True, stderr=subprocess.STDOUT, env=env).strip()
        except (OSError, subprocess.CalledProcessError):
            record["versions"][command[0]] = None

    tests = [(name, [f"scripts/test-{name}.sh"]) for name in SHELL_SUITES]
    tests += [(f"python-{name}", [sys.executable, f"scripts/tests/test-{name}.py"])
              for name in STANDARD_PYTHON_SUITES]
    tests += [(f"python-{name}", [sys.executable, "-O", f"scripts/tests/test-{name}.py"])
              for name in OPTIMIZED_PYTHON_SUITES]
    tests += [("face-studio", ["node", "scripts/tests/test-face-studio.js"]),
              ("v4-vectors", [sys.executable, "scripts/tests/v4/make_vectors.py", "--check"])]
    manifest = output / "manifest.json"
    for name, command in tests:
        log = output / f"{name}.log"
        with log.open("w") as stream:
            try:
                result = subprocess.run(command, cwd=ROOT, env=env, stdout=stream,
                                        stderr=subprocess.STDOUT, check=False)
                code = result.returncode
            except OSError as error:
                stream.write(str(error) + "\n")
                code = None
        # Retain runner-reported skips separately from the process exit status.
        skips = [line.strip() for line in log.read_text(errors="replace").splitlines()
                 if line.lstrip().upper().startswith(("SKIP", "OK (SKIPPED="))]
        record["results"].append({"command": command, "exit_code": code,
                                  "log": str(log.relative_to(ROOT)), "reported_skips": skips})
        print(f"{code} {shlex.join(command)}", flush=True)
        write_json(manifest, record)
    record["inputs_unchanged"] = source_identity(ROOT) == record["source"]
    write_json(manifest, record)
    return 0 if record["inputs_unchanged"] and all(r["exit_code"] == 0 for r in record["results"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
