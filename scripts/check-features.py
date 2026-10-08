#!/usr/bin/env python3
"""Compile every sensor combination, regardless of the active sketch's flags."""
import itertools
from pathlib import Path
import re
import shutil
import tempfile

FEATURES = ("VisorSensor", "BoopSensor", "Microphone")
ALL_FEATURES = FEATURES + ("WirelessPaw", "VisorConnector")
FQBN = "teensy:avr:teensy40:usb=serial,speed=600,opt=o2std"


# Keep this import local to the scripts directory, including runpy-based callers.
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_support import BuildRun, check_toolchain, find_cli


def feature_declaration(source, name):
    pattern = re.compile(
        r"^([ \t]*)(//[ \t]*)?#[ \t]*define[ \t]+" + name + r"(?=[ \t]|$)([^\r\n]*)$",
        re.MULTILINE,
    )
    matches = list(pattern.finditer(source))
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one declaration for {name}; found {len(matches)}")
    return pattern, matches[0]


def enabled_features(source):
    """Validate and identify all sensor and extension flags in the built snapshot."""
    return [name for name in ALL_FEATURES if feature_declaration(source, name)[1][2] is None]


def configure_features(source, enabled, names=FEATURES):
    """Set both enabled AND disabled flags; reject ambiguous/missing declarations."""
    if set(enabled) - set(names) or set(names) - set(ALL_FEATURES):
        raise ValueError("Unknown feature")
    for name in names:
        pattern, _ = feature_declaration(source, name)
        prefix = "#define " if name in enabled else "// #define "
        source = pattern.sub(lambda match: match[1] + prefix + name + match[3], source)
    return source


def main():
    root = Path(__file__).resolve().parents[1]
    evidence = BuildRun(root, "features")
    try:
        snapshot = evidence.snapshot()
        source = (snapshot / "PROT_OS7_MAX7219.ino").read_text()
        configure_features(source, (), ALL_FEATURES)
        cli = find_cli()
        evidence.record["toolchain"] = check_toolchain(root, cli)
        for flags in itertools.product((False, True), repeat=len(FEATURES)):
            names = [name for name, enabled in zip(FEATURES, flags) if enabled]
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
