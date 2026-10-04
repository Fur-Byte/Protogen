#!/usr/bin/env python3
"""Regression checks for configuration validation and reliable feature coverage."""
import importlib.util
import itertools
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("feature_checks", ROOT / "scripts/check-features.py")
features = importlib.util.module_from_spec(spec)
spec.loader.exec_module(features)


class FeatureChecks(unittest.TestCase):
    def test_all_initial_and_requested_combinations(self):
        original = (ROOT / "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino").read_text()
        combinations = [tuple(name for name, flag in zip(features.FEATURES, flags) if flag)
                        for flags in itertools.product((False, True), repeat=3)]
        for initial in combinations:
            source = features.configure_features(original, initial)
            for enabled in combinations:
                with self.subTest(initial=initial, enabled=enabled):
                    actual = features.configure_features(source, enabled)
                    active = set(re.findall(r"^#define (VisorSensor|BoopSensor|Microphone)\b",
                                            actual, re.MULTILINE))
                    self.assertEqual(active, set(enabled))

    def test_missing_and_duplicate_flags_fail(self):
        source = "\n".join("// #define " + name for name in features.FEATURES)
        for malformed in (source.replace("// #define Microphone", ""), source + "\n#define Microphone"):
            with self.assertRaises(ValueError):
                features.configure_features(malformed, ())

    def test_explicit_missing_cli_does_not_fall_back(self):
        with patch.dict(os.environ, {"ARDUINO_CLI": "/missing/protogen/arduino-cli"}):
            with self.assertRaises(SystemExit):
                features.find_cli()

    def test_cli_override_accepts_path_commands(self):
        with tempfile.TemporaryDirectory(prefix="protogen-cli-test-") as tmp:
            executable = Path(tmp) / "custom-cli"
            executable.write_text("#!/bin/sh\nexit 0\n")
            executable.chmod(0o755)
            with patch.dict(os.environ, {"PATH": tmp, "ARDUINO_CLI": "custom-cli"}):
                self.assertEqual(features.find_cli(), str(executable))

    def test_failures_are_returned_even_with_optimization(self):
        # A mock compiler reports either an error or a warning. Keep these tests'
        # reports in a disposable project, without overwriting real build evidence.
        with tempfile.TemporaryDirectory(prefix="protogen-feature-result-") as tmp:
            root = Path(tmp)
            source = root / "firmware/PROT_OS7_MAX7219"
            source.mkdir(parents=True)
            (source / "PROT_OS7_MAX7219.ino").write_text(
                "\n".join("// #define " + name for name in features.FEATURES))
            script = root / "scripts/check-features.py"
            script.parent.mkdir()
            script.write_bytes((ROOT / "scripts/check-features.py").read_bytes())
            compiler = root / "fake-cli"
            for output, status in (("error: simulated", 1), ("warning: simulated", 0)):
                compiler.write_text(f"#!/bin/sh\nprintf '%s\\n' '{output}'\nexit {status}\n")
                compiler.chmod(0o755)
                run = subprocess.run(
                    [os.sys.executable, "-O", str(script)], capture_output=True, text=True,
                    env=dict(os.environ, ARDUINO_CLI=str(compiler)))
                self.assertEqual(run.returncode, 1, run.stdout + run.stderr)


class SettingsChecks(unittest.TestCase):
    def test_invalid_settings_rejected_before_narrowing(self):
        source = (ROOT / "firmware/PROT_OS7_MAX7219/Settings.h").read_text()
        mutations = [("Brightness", "256"), ("Brightness", "-1"),
                     ("StartupExpression", "256"), ("StartupExpression", "-1"),
                     ("BoopThreshold", "550"), ("MicrophoneSensitivity", "1024"),
                     ("ButtonHoldMs", "10")]
        intervals = re.findall(r"constexpr uint32_t (\w+Ms) =", source)
        mutations += [(name, value) for name in intervals for value in ("0", "0xffffffffUL")]
        for name, value in [(None, None)] + mutations:
            with self.subTest(setting=name, value=value):
                text = source if name is None else re.sub(
                    r"(constexpr \w+ " + name + r" = )[^;]+", lambda match: match[1] + value, source)
                # Compile as an included header so #pragma once is not a warning.
                with tempfile.TemporaryDirectory(prefix="protogen-settings-") as tmp:
                    header = Path(tmp) / "Settings.h"
                    header.write_text(text)
                    run = subprocess.run(
                        [os.environ.get("CXX", "clang++"), "-std=c++17", "-x", "c++",
                         "-fsyntax-only", "-include", str(header), "-"],
                        input="", capture_output=True, text=True)
                if name is None:
                    self.assertEqual(run.returncode, 0, run.stderr)
                else:
                    self.assertNotEqual(run.returncode, 0)
                    self.assertIn("static assertion failed", run.stderr)

    def test_wrong_board_is_rejected(self):
        run = subprocess.run(
            [os.environ.get("CXX", "clang++"), "-std=c++17", "-DARDUINO=10819",
             "-I" + str(ROOT / "scripts/tests/stubs"),
             "-I" + str(ROOT / "firmware/PROT_OS7_MAX7219"),
             "-fsyntax-only", str(ROOT / "scripts/tests/feature-smoke.cpp")],
            capture_output=True, text=True)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("Select Teensy 4.0", run.stderr)


if __name__ == "__main__":
    unittest.main()
