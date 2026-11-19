#!/usr/bin/env python3
"""Compile every sensor combination, regardless of the active sketch's flags."""
import itertools
from pathlib import Path
import shutil
import sys
import tempfile

from build_support import BuildRun, check_toolchain, find_cli
from sketch_features import ALL_FEATURES, SENSOR_FEATURES, configure_features


def main():
    root = Path(__file__).resolve().parents[1]
    evidence = BuildRun(root, "features")
    try:
        snapshot = evidence.snapshot()
        source = (snapshot / "PROT_OS7_MAX7219.ino").read_text()
        configure_features(source, (), ALL_FEATURES)
        cli = find_cli()
        evidence.record["toolchain"] = check_toolchain(root, cli)
        for flags in itertools.product((False, True), repeat=len(SENSOR_FEATURES)):
            names = [name for name, enabled in zip(SENSOR_FEATURES, flags) if enabled]
            with tempfile.TemporaryDirectory(prefix="protogen-features-") as tmp:
                sketch = Path(tmp) / "PROT_OS7_MAX7219"
                shutil.copytree(snapshot, sketch)
                (sketch / "PROT_OS7_MAX7219.ino").write_text(configure_features(source, names, ALL_FEATURES))
                evidence.compile(cli, sketch, names)
        return 0 if evidence.finish() else 1
    except (Exception, SystemExit, KeyboardInterrupt) as error:
        evidence.finish(str(error) or "Feature checks interrupted")
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
