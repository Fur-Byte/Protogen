#!/usr/bin/env python3
"""Build the selected features; publish a HEX only after zero-warning validation."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
from build_support import BuildRun, check_toolchain, find_cli, write_json


def main():
    root = Path(__file__).resolve().parents[1]
    run = BuildRun(root, "build")
    try:
        sketch = run.snapshot()
        spec = importlib.util.spec_from_file_location("feature_checks", root / "scripts/check-features.py")
        feature_checks = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(feature_checks)
        source = (sketch / "PROT_OS7_MAX7219.ino").read_text()
        feature_checks.configure_features(source, ()) # Validate every feature declaration.
        enabled = re.findall(r"^\s*#define (VisorSensor|BoopSensor|Microphone)\b", source, re.MULTILINE)
        cli = find_cli()
        run.record["toolchain"] = check_toolchain(root, cli)
        output = run.compile(cli, sketch, enabled)
        if not run.finish():
            print("Build failed; previous HEX remains identified by its own manifest.", file=sys.stderr)
            return 1
        hex_files = sorted(output.glob("*.hex"))
        if not hex_files:
            raise RuntimeError("Compiler returned success without exporting a HEX")
        run.record["artifacts"] = {p.name: __import__('hashlib').sha256(p.read_bytes()).hexdigest()
                                   for p in output.iterdir() if p.is_file()}
        run.publish()
        # Publish the compatibility directory as a whole, including its source manifest.
        staging = Path(tempfile.mkdtemp(prefix="protogen-export-", dir=run.build))
        try:
            for path in output.iterdir():
                if path.is_file():
                    shutil.copy2(path, staging / path.name)
            write_json(staging / "manifest.json", run.record)
            destination = run.build / "teensy40"
            previous = run.path / "previous-export"
            if destination.exists():
                os.replace(destination, previous)
            try:
                os.replace(staging, destination)
            except BaseException:
                if previous.exists():
                    os.replace(previous, destination)
                raise
        finally:
            if staging.exists():
                shutil.rmtree(staging)
        write_json(run.build / "latest-successful-build.json", run.record)
        print("HEX and source manifest: " + str(run.build / "teensy40"), flush=True)
        return 0
    except (Exception, SystemExit, KeyboardInterrupt) as error:
        run.finish(str(error) or "Build interrupted")
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
