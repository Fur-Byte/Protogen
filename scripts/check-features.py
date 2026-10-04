#!/usr/bin/env python3
"""Compile every sensor combination, regardless of the active sketch's flags."""
import itertools
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

FEATURES = ("VisorSensor", "BoopSensor", "Microphone")
FQBN = "teensy:avr:teensy40:usb=serial,speed=600,opt=o2std"


def find_cli():
    override = os.environ.get("ARDUINO_CLI")
    candidates = [override] if override else [
        "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli",
        "arduino-cli",
    ]
    for candidate in candidates:
        resolved = shutil.which(candidate)
        if resolved:
            return resolved
    raise SystemExit("Arduino CLI not found; set ARDUINO_CLI to an executable path or command.")


def configure_features(source, enabled):
    """Set both enabled AND disabled flags; reject ambiguous/missing declarations."""
    if set(enabled) - set(FEATURES):
        raise ValueError("Unknown sensor feature")
    for name in FEATURES:
        pattern = re.compile(
            r"^([ \t]*)(?://[ \t]*)?#[ \t]*define[ \t]+" + name + r"(?=[ \t]|$)([^\r\n]*)$",
            re.MULTILINE,
        )
        prefix = "#define " if name in enabled else "// #define "
        source, count = pattern.subn(lambda match: match[1] + prefix + name + match[2], source)
        if count != 1:
            raise ValueError(f"Expected exactly one declaration for {name}; found {count}")
    return source


def main():
    root = Path(__file__).resolve().parents[1]
    cli = find_cli()
    build = root / "build"
    build.mkdir(exist_ok=True)
    results = []
    source = (root / "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino").read_text()
    # Validate all declarations before creating a success/failure report.
    configure_features(source, ())
    with (build / "feature-checks.log").open("w") as log:
        for flags in itertools.product((False, True), repeat=len(FEATURES)):
            names = [name for name, enabled in zip(FEATURES, flags) if enabled]
            with tempfile.TemporaryDirectory(prefix="protogen-features-") as tmp:
                sketch = Path(tmp) / "PROT_OS7_MAX7219"
                shutil.copytree(root / "firmware/PROT_OS7_MAX7219", sketch)
                (sketch / "PROT_OS7_MAX7219.ino").write_text(configure_features(source, names))
                run = subprocess.run(
                    [cli, "compile", "--fqbn", FQBN, "--warnings", "all", "--clean",
                     "--build-path", str(Path(tmp) / "obj"), str(sketch)],
                    capture_output=True, text=True,
                )
            output = run.stdout + run.stderr
            result = {"features": names, "exit_code": run.returncode,
                      "warnings": len(re.findall(r"\bwarning:", output))}
            results.append(result)
            log.write(json.dumps(result) + "\n" + output + "\n")
            log.flush()
            # Keep completed cases available even if a later build is interrupted.
            (build / "feature-results.json").write_text(json.dumps(results, indent=2) + "\n")
            print(result, flush=True)
    # An assert would silently disappear with python -O / PYTHONOPTIMIZE.
    return 0 if all(r["exit_code"] == 0 and r["warnings"] == 0 for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
