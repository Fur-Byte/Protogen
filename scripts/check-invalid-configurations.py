#!/usr/bin/env python3
"""Check positive compile controls and require the intended rejection diagnostics."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, default=ROOT / "firmware")
    args = parser.parse_args()
    teensy = args.firmware / "PROT_OS7_MAX7219"
    esp32 = args.firmware / "esp32"
    with tempfile.TemporaryDirectory(prefix="protogen-invalid-") as tmp:
        fixture = Path(tmp)
        (fixture / "sdkconfig.h").write_text("")
        (fixture / "bad-ceiling.h").write_text(re.sub(
            r"(constexpr int MaxBrightness = )\d+;", r"\g<1>16;", (teensy / "Settings.h").read_text()))
        command = [os.environ.get("CXX", "clang++"), "-std=c++17", "-x", "c++", "-fsyntax-only",
                   f"-I{fixture}", f"-I{teensy}", f"-I{teensy / 'src/HelmetProtocol'}",
                   f"-I{esp32 / 'src'}", f"-I{esp32 / 'protocol_v4'}"]
        probes = (
            ("efficiency-valid", "Efficiency.h", ("PROTOGEN_MAIN_EVENTS=1",), None),
            ("efficiency-invalid", "Efficiency.h", ("PROTOGEN_MAIN_EVENTS=2",), "Bench flags must be 0 or 1"),
            ("render-valid", "Settings.h", ("PROTOGEN_EARLY_EXPRESSION_REVEAL=1",), None),
            ("render-invalid", "Settings.h", ("PROTOGEN_EARLY_EXPRESSION_REVEAL=2",),
             "Early expression reveal must be 0 or 1"),
            ("animation-v2-valid", "src/BehaviorCore/AnimationV2.h", ("PROTOGEN_ANIMATION_V2=1",), None),
            ("animation-v2-invalid", "src/BehaviorCore/AnimationV2.h", ("PROTOGEN_ANIMATION_V2=2",),
             "Animation V2 must be 0 or 1"),
            ("timeline-valid", "src/BehaviorCore/Timeline.h",
             ("PROTOGEN_ANIMATION_V2=1", "PROTOGEN_ANIMATION_TIMELINES=1"), None),
            ("timeline-missing-v2", "src/BehaviorCore/Timeline.h",
             ("PROTOGEN_ANIMATION_TIMELINES=1",), "Animation timelines require Animation V2"),
            ("timeline-invalid", "src/BehaviorCore/Timeline.h",
             ("PROTOGEN_ANIMATION_V2=1", "PROTOGEN_ANIMATION_TIMELINES=2"), "Animation timelines must be 0 or 1"),
            ("brightness-invalid", "bad-ceiling.h", (), "MaxBrightness must be 0..15"),
            ("board-valid", "DeviceConfig.h", ("ESP_PLATFORM", "CONFIG_IDF_TARGET_ESP32C3=1"), None),
            ("board-invalid", "DeviceConfig.h", ("ESP_PLATFORM", "CONFIG_IDF_TARGET_ESP32S31=1"),
             "NO_QUALIFIED_BOARD_MAPPING_USE_C3_OR_ISOLATED_S31_PREFLIGHT"),
            ("v4-host", "Interfaces.hpp", (), None),
            ("v4-activation-invalid", "Interfaces.hpp", ("ESP_PLATFORM",),
             "EXPERIMENTAL_V4_PLATFORM_ACTIVATION_BLOCKED"),
        )
        for name, header, macros, diagnostic in probes:
            result = subprocess.run(command + ["-D" + flag for flag in macros] + ["-"],
                                    input=f'#include "{header}"\n', capture_output=True, text=True)
            if diagnostic is None and result.returncode:
                parser.exit(1, f"{name}: positive control failed\n{result.stderr}")
            if diagnostic and (result.returncode == 0 or diagnostic not in result.stderr):
                parser.exit(1, f"{name}: compile must fail with {diagnostic!r}\n{result.stderr}")
            print(f"PASS: {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
