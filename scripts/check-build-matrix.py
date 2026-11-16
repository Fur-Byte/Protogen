#!/usr/bin/env python3
"""Build isolated Teensy variants and both C3 roles; never flash or provision hardware."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

from build_support import BuildRun, check_toolchain, find_cli, git_identity, write_json

ROOT = Path(__file__).resolve().parents[1]


def load(script):
    spec = importlib.util.spec_from_file_location(script, ROOT / "scripts" / (script + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


FEATURES = load("check-features")
ALL = (1 << len(FEATURES.ALL_FEATURES)) - 1
PAW = 1 << FEATURES.ALL_FEATURES.index("WirelessPaw")
# All sensors, each extension alone, plus the feature interactions at full mask.
FAST = {False: (0, 7, PAW, 16, ALL), True: (PAW, ALL)}
BENCH = {"render-service": ("CACHE_FACE", "MAIN_EVENTS"),
         "oled-checkpoint": ("OLED_1HZ", "OLED_PAGES", "OLED_PROTECTION", "CHECKPOINT_5MIN"),
         "wearer-service": ("CASE_2HZ", "WEARER_DEADLINES", "MAIN_EVENTS")}


def temporary_key(directory):
    subprocess.run([sys.executable, str(ROOT / "scripts/provision-uart-auth.py"),
                    "--output", str(directory)], check=True)


def linked_uart(artifact):
    units = json.loads((artifact / "objects/compile_commands.json").read_text())
    compiler = Path(units[0]["arguments"][0])
    symbols = subprocess.check_output([str(compiler.with_name("arm-none-eabi-nm")),
        "--defined-only", "-C", str(artifact / "objects/PROT_OS7_MAX7219.ino.elf")], text=True)
    return all(symbol in symbols for symbol in ("protogen_uart_mbedtls_md_hmac_starts",
               "protogen_uart_mbedtls_md_hmac_finish", "PawLink::uartMac"))


def teensy(mode, group, jobs):
    cases = []
    for auth in (False, True):
        if group == ("plain" if auth else "auth"):
            continue
        masks = FAST[auth] if mode == "fast" else range(ALL + 1)
        cases.extend((mask, auth, "production", ()) for mask in masks if not auth or mask & PAW)
    if group in ("all", "auth"):
        selected = BENCH.items() if mode == "full" else [("render-service", BENCH["render-service"])]
        cases.extend((ALL, True, name, flags) for name, flags in selected)
    run = BuildRun(ROOT, f"matrix-{mode}-{group}")
    run.record["planned_cases"] = [{"mask": m, "uart_auth": a, "profile": p,
                                    "efficiency_flags": f} for m, a, p, f in cases]
    try:
        sketch = run.snapshot()
        main = sketch / "PROT_OS7_MAX7219.ino"
        original = main.read_text()
        cli = find_cli()
        run.record["toolchain"] = check_toolchain(ROOT, cli)
        with tempfile.TemporaryDirectory(prefix="protogen-matrix-") as tmp:
            key = Path(tmp) / "key"
            if any(auth for _, auth, _, _ in cases):
                temporary_key(key)
            for mask, auth, profile, flags in cases:
                enabled = [name for bit, name in enumerate(FEATURES.ALL_FEATURES) if mask & (1 << bit)]
                main.write_text(FEATURES.configure_features(original, enabled, FEATURES.ALL_FEATURES))
                definitions = ["-Werror", *("-DPROTOGEN_" + flag + "=1" for flag in flags)]
                if auth:
                    enabled.append("PROTOGEN_UART_AUTH")
                    definitions.extend(("-DPROTOGEN_UART_AUTH=1", "-I" + str(key)))
                artifact = run.compile(cli, sketch, enabled, extra_flags=shlex.join(definitions),
                                       reuse_cache=True, jobs=jobs)
                result = run.record["results"][-1]
                result.update(mask=mask, uart_auth=auth, profile=profile, efficiency_flags=flags)
                if auth and result["exit_code"] == 0 and not result.get("dependency_error"):
                    if not linked_uart(artifact):
                        result["dependency_error"] = "Authenticated UART symbols absent from linked Teensy image"
                run.publish()
        return run.finish()
    except (Exception, KeyboardInterrupt) as error:
        print(error, file=sys.stderr)
        return run.finish(str(error))


def esp32(mode, jobs):
    output = ROOT / f"build/matrix-{mode}-esp32"
    output.mkdir(parents=True, exist_ok=True)
    evidence = {"schema": 1, **git_identity(ROOT), "planned_roles": ["helmet", "paw"],
                "planned_uart_auth": [True, False], "results": []}
    manifest = output / "manifest.json"
    write_json(manifest, evidence)
    with tempfile.TemporaryDirectory(prefix="protogen-matrix-") as tmp:
        key = Path(tmp) / "key"
        temporary_key(key)
        for auth in (True, False):
            env = dict(os.environ, PROTOGEN_PIO_JOBS=str(jobs))
            env.pop("PROTOGEN_UART_KEY_DIR", None)
            if auth:
                env["PROTOGEN_UART_KEY_DIR"] = str(key)
            command = [sys.executable, str(ROOT / "scripts/build-esp32.py")]
            # Sequential variants share IDF dependencies and leave the default build last.
            compiled = subprocess.run(command, env=env)
            record = json.loads((ROOT / "build/esp32-build.json").read_text())
            log = ROOT / "build/esp32-build.log"
            if log.is_file() and "exit_code" in record:
                (output / f"uart-auth-{int(auth)}.log").write_bytes(log.read_bytes())
            evidence["results"].append({"command": command, "requested_uart_auth": auth,
                                        "exit_code": compiled.returncode, "build": record})
            write_json(manifest, evidence)
    return all(result["exit_code"] == 0 and result["build"]["state"] == "success"
               and result["build"]["uart_auth"] == result["requested_uart_auth"]
               for result in evidence["results"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("fast", "full"))
    parser.add_argument("target", choices=("teensy", "esp32"))
    parser.add_argument("--group", choices=("all", "plain", "auth"), default="all")
    parser.add_argument("--jobs", type=int, default=4, choices=range(1, 9), metavar="1..8")
    args = parser.parse_args()
    if args.target == "esp32":
        if args.group != "all":
            parser.error("--group applies to Teensy builds only")
        return 0 if esp32(args.mode, args.jobs) else 1
    return 0 if teensy(args.mode, args.group, args.jobs) else 1


if __name__ == "__main__":
    raise SystemExit(main())
