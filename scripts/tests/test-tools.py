#!/usr/bin/env python3
"""Regression checks for configuration validation and reliable feature coverage."""
import importlib.util
import hashlib
import json
import shutil
import itertools
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import sketch_features as features
from build_support import BuildRun, find_cli


class FeatureChecks(unittest.TestCase):
    def test_platformio_mcp_outputs_do_not_change_source_identity(self):
        from build_support import source_identity
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'firmware/esp32/src/main.cpp'
            source.parent.mkdir(parents=True)
            source.write_text('int value = 1;')
            before = source_identity(root)
            log = root / 'firmware/esp32/.pio-mcp-workspace/logs/build.log'
            log.parent.mkdir(parents=True)
            log.write_text('Generated diagnostic output')
            self.assertEqual(source_identity(root), before)
            database = root / 'firmware/esp32/compile_commands.json'
            database.write_text('[]')
            self.assertEqual(source_identity(root), before)
            source.write_text('int value = 2;')
            self.assertNotEqual(source_identity(root), before)

    def test_all_initial_and_requested_combinations(self):
        original = (ROOT / "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino").read_text()
        combinations = [tuple(name for name, flag in zip(features.SENSOR_FEATURES, flags) if flag)
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
        source = "\n".join("// #define " + name for name in features.SENSOR_FEATURES)
        for malformed in (source.replace("// #define Microphone", ""), source + "\n#define Microphone"):
            with self.assertRaises(ValueError):
                features.configure_features(malformed, ())

    def test_all_sensor_and_extension_combinations_are_identified(self):
        original = (ROOT / "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino").read_text()
        combinations = [tuple(name for name, flag in zip(features.ALL_FEATURES, flags) if flag)
                        for flags in itertools.product((False, True), repeat=len(features.ALL_FEATURES))]
        for initial in combinations:
            source = features.configure_features(original, initial, features.ALL_FEATURES)
            for enabled in combinations:
                with self.subTest(initial=initial, enabled=enabled):
                    configured = features.configure_features(source, enabled, features.ALL_FEATURES)
                    self.assertEqual(features.enabled_features(configured), list(enabled))
            # Preview callers configure sensors without silently enabling extensions.
            configured = features.configure_features(source, features.SENSOR_FEATURES)
            self.assertEqual(features.enabled_features(configured),
                             list(features.SENSOR_FEATURES) + [n for n in initial if n not in features.SENSOR_FEATURES])

    def test_all_feature_declarations_are_validated(self):
        source = "\n".join("\t//\t# define\t" + name + " // optional" for name in features.ALL_FEATURES)
        self.assertEqual(features.enabled_features(source), [])
        for name in features.ALL_FEATURES:
            with self.subTest(feature=name):
                configured = features.configure_features(source, (name,), features.ALL_FEATURES)
                self.assertEqual(features.enabled_features(configured), [name])
                self.assertIn(" // optional", configured)
                for malformed in (configured.replace("#define " + name, "#define RemovedFlag"),
                                  configured + "\n#define " + name):
                    with self.assertRaisesRegex(ValueError, "exactly one declaration"):
                        features.enabled_features(malformed)
        with self.assertRaises(ValueError):
            features.configure_features(source, ("Unknown",), features.ALL_FEATURES)

    def test_sensor_matrix_disables_extensions_from_active_source(self):
        def compile_fixture(run, cli, sketch, enabled):
            actual = features.enabled_features((sketch / "PROT_OS7_MAX7219.ino").read_text())
            self.assertEqual(actual, enabled)
            self.assertFalse(set(actual) & {"WirelessPaw", "VisorConnector"})
            run.record["results"].append({"features": list(enabled), "exit_code": 0, "warnings": 0})

        with tempfile.TemporaryDirectory(prefix="protogen-sensor-matrix-") as tmp:
            root = Path(tmp)
            self.make_project(root)
            source = root / "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino"
            source.write_text(features.configure_features(source.read_text(), features.ALL_FEATURES, features.ALL_FEATURES))
            spec = importlib.util.spec_from_file_location("matrix_fixture", root / "scripts/check-features.py")
            matrix = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(matrix)
            with patch.object(matrix, "find_cli", return_value="synthetic-cli"), \
                    patch.object(matrix, "check_toolchain", return_value={}), \
                    patch.object(matrix.BuildRun, "compile", compile_fixture):
                self.assertEqual(matrix.main(), 0)
            record = json.loads((root / "build/feature-results.json").read_text())
            self.assertEqual(record["state"], "success")
            self.assertEqual(len(record["results"]), 8)

    def test_explicit_missing_cli_does_not_fall_back(self):
        with patch.dict(os.environ, {"ARDUINO_CLI": "/missing/protogen/arduino-cli"}):
            with self.assertRaises(RuntimeError):
                find_cli()

    def test_cli_override_accepts_path_commands(self):
        with tempfile.TemporaryDirectory(prefix="protogen-cli-test-") as tmp:
            executable = Path(tmp) / "custom-cli"
            executable.write_text("#!/bin/sh\nexit 0\n")
            executable.chmod(0o755)
            with patch.dict(os.environ, {"PATH": tmp, "ARDUINO_CLI": "custom-cli"}):
                self.assertEqual(find_cli(), str(executable))

    def make_project(self, root):
        source = root / "firmware/PROT_OS7_MAX7219"
        source.mkdir(parents=True)
        (source / "PROT_OS7_MAX7219.ino").write_text(
            "\n".join("// #define " + name for name in features.ALL_FEATURES))
        (root / "scripts").mkdir()
        for name in ("check-features.py", "sketch_features.py", "build_support.py", "build.py", "uart_key.py"):
            shutil.copy2(ROOT / "scripts" / name, root / "scripts" / name)
        shutil.copy2(ROOT / "toolchain.lock.json", root / "toolchain.lock.json")

    def compiler(self, root, output, status):
        locked = json.loads((ROOT / "toolchain.lock.json").read_text())
        compiler = root / "fake-cli"
        compiler.write_text(
            "#!/bin/sh\ncase \"$1\" in\n" +
            "version) printf '%s\\n' '" + json.dumps({"VersionString": locked["arduino_cli"]}) + "'; exit 0;;\n" +
            "core) printf '%s\\n' '" + json.dumps({"platforms": [{"id": locked["platform"]["id"], "installed_version": locked["platform"]["version"]}]}) + "'; exit 0;;\n" +
            "lib) printf '%s\\n' '" + json.dumps({"installed_libraries": [{"library": {"name": name, "version": version}} for name, version in locked["libraries"].items()]}) + "'; exit 0;;\n" +
            "compile) case \"$*\" in *--show-properties=expanded*) printf '%s\\n' 'build.flags.defs=-D__IMXRT1062__ -DTEENSYDUINO=160'; exit 0;; esac;;\n" +
            "esac\nprintf '%s\\n' '" + output + "'\nexit " + str(status) + "\n")
        compiler.chmod(0o755)
        return compiler

    def test_failures_are_returned_even_with_optimization(self):
        with tempfile.TemporaryDirectory(prefix="protogen-feature-result-") as tmp:
            root = Path(tmp)
            self.make_project(root)
            for output, status in (("error: simulated", 1), ("warning: simulated", 0)):
                compiler = self.compiler(root, output, status)
                run = subprocess.run(
                    [os.sys.executable, "-O", str(root / "scripts/check-features.py")], capture_output=True, text=True,
                    env=dict(os.environ, ARDUINO_CLI=str(compiler)))
                self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
                report = json.loads((root / "build/feature-results.json").read_text())
                self.assertEqual(report["state"], "failed")
                self.assertEqual(len(report["results"]), 8)
                self.assertTrue(report["source"]["sha256"])
                self.assertTrue(report["toolchain"]["arduino_cli"])

    def test_preflight_failures_replace_older_success_evidence(self):
        with tempfile.TemporaryDirectory(prefix="protogen-feature-preflight-") as tmp:
            root = Path(tmp)
            self.make_project(root)
            build = root / "build"
            build.mkdir()
            (build / "feature-results.json").write_text('{"state":"success"}')
            source = root / "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino"
            source.write_text(source.read_text() + "\n#define Microphone\n")
            run = subprocess.run([os.sys.executable, str(root / "scripts/check-features.py")], capture_output=True, text=True)
            self.assertEqual(run.returncode, 1)
            report = json.loads((build / "feature-results.json").read_text())
            self.assertEqual(report["state"], "failed")
            self.assertEqual(report["results"], [])
            self.assertIn("exactly one declaration", report["error"])

    def test_warning_build_preserves_identified_previous_hex(self):
        with tempfile.TemporaryDirectory(prefix="protogen-build-warning-") as tmp:
            root = Path(tmp)
            self.make_project(root)
            output = root / "build/teensy40"
            output.mkdir(parents=True)
            (output / "original.hex").write_text("old firmware")
            compiler = self.compiler(root, "warning: simulated", 0)
            run = subprocess.run([os.sys.executable, str(root / "scripts/build.py")], capture_output=True, text=True,
                                 env=dict(os.environ, ARDUINO_CLI=str(compiler)))
            self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
            report = json.loads((root / "build/current-build.json").read_text())
            self.assertEqual(report["state"], "failed")
            self.assertEqual((output / "original.hex").read_text(), "old firmware")

    def test_published_manifest_includes_selected_extensions(self):
        def compile_fixture(run, cli, sketch, enabled):
            output = run.path / "artifacts-0"
            output.mkdir()
            (output / "fixture.hex").write_text("synthetic build output")
            run.record["results"].append({"features": list(enabled), "exit_code": 0, "warnings": 0})
            return output

        for enabled in (("WirelessPaw",), ("VisorConnector",), ("WirelessPaw", "VisorConnector")):
            with self.subTest(enabled=enabled), tempfile.TemporaryDirectory(prefix="protogen-manifest-") as tmp:
                root = Path(tmp)
                self.make_project(root)
                (root / "scripts/check-features.py").unlink()
                source = root / "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino"
                source.write_text(features.configure_features(source.read_text(), enabled, features.ALL_FEATURES))
                spec = importlib.util.spec_from_file_location("build_fixture", root / "scripts/build.py")
                build = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(build)
                with patch.object(build, "find_cli", return_value="synthetic-cli"), \
                        patch.object(build, "check_toolchain", return_value={}), \
                        patch.object(build.BuildRun, "compile", compile_fixture), patch("builtins.print"):
                    self.assertEqual(build.main(), 0)
                published = json.loads((root / "build/teensy40/manifest.json").read_text())
                self.assertEqual(published["state"], "success")
                self.assertEqual(published["results"][0]["features"], list(enabled))
                self.assertEqual(published, json.loads((root / "build/latest-successful-build.json").read_text()))
                self.assertIn("fixture.hex", published["artifacts"])

    def test_connector_only_build_requires_selected_pinned_adc(self):
        with tempfile.TemporaryDirectory(prefix="protogen-connector-adc-") as tmp:
            root = Path(tmp)
            self.make_project(root)
            source = root / "firmware/PROT_OS7_MAX7219/PROT_OS7_MAX7219.ino"
            source.write_text(features.configure_features(source.read_text(), ("VisorConnector",), features.ALL_FEATURES))
            enabled = features.enabled_features(source.read_text())
            run = BuildRun(root, "build")
            sketch = run.snapshot()
            locked = json.loads((root / "toolchain.lock.json").read_text())
            run.record["toolchain"] = {"libraries": {}, "bundled_libraries": locked["bundled_libraries"]}
            library = root / "ADC"
            library.mkdir()
            output = root / "output"
            objects = output / "objects"
            objects.mkdir(parents=True)
            (objects / "compile_commands.json").write_text(json.dumps([{"arguments": ["c++", "-Werror"]}]))
            with patch("subprocess.run", return_value=subprocess.CompletedProcess(
                    [], 0, "build.flags.defs=-D__IMXRT1062__ -DTEENSYDUINO=160\n", "")), patch("builtins.print"):
                for selected, version in (([], None), ([str(library)], "0.0.0"),
                                          ([str(library)], locked["bundled_libraries"]["ADC"])):
                    with self.subTest(selected=selected, version=version):
                        (objects / "libraries.cache").write_text(json.dumps(selected))
                        if version:
                            (library / "library.properties").write_text("name=ADC\nversion=" + version + "\n")
                        run.compile("synthetic-cli", sketch, enabled, output)
                        result = run.record["results"][-1]
                        if version == locked["bundled_libraries"]["ADC"]:
                            self.assertNotIn("dependency_error", result)
                            self.assertEqual(result["selected_libraries"]["ADC"]["version"], version)
                        else:
                            self.assertIn("ADC", result["dependency_error"])

    def test_edit_during_run_invalidates_success(self):
        with tempfile.TemporaryDirectory(prefix="protogen-source-change-") as tmp:
            root = Path(tmp)
            self.make_project(root)
            run = BuildRun(root, "build")
            run.snapshot()
            run.record["results"] = [{"exit_code": 0, "warnings": 0}]
            (root / "firmware/PROT_OS7_MAX7219/Settings.h").write_text("// Changed during build\n")
            self.assertFalse(run.finish())
            self.assertEqual(run.record["state"], "failed")
            self.assertIn("Source changed", run.record["error"])

    def test_selected_entropy_source_must_match_lock(self):
        with tempfile.TemporaryDirectory(prefix="protogen-entropy-lock-") as tmp:
            root = Path(tmp)
            self.make_project(root)
            library = root / "Entropy"
            library.mkdir()
            sources = {"Entropy.h": b"header", "Entropy.cpp": b"hardware implementation"}
            for name, content in sources.items():
                (library / name).write_bytes(content)
            run = BuildRun(root, "build")
            sketch = run.snapshot()
            run.record["toolchain"] = {"libraries": {}, "bundled_libraries": {},
                "bundled_source_libraries": {"Entropy": {
                    name: hashlib.sha256(content).hexdigest() for name, content in sources.items()}}}
            output = root / "output"
            (output / "objects").mkdir(parents=True)
            (output / "objects/libraries.cache").write_text(json.dumps([str(library)]))
            (output / "objects/compile_commands.json").write_text(json.dumps([{"arguments": ["c++", "-Werror"]}]))
            with patch("subprocess.run", return_value=subprocess.CompletedProcess(
                    [], 0, "build.flags.defs=-D__IMXRT1062__ -DTEENSYDUINO=160\n", "")):
                run.compile("fake-cli", sketch, (), output)
                self.assertNotIn("dependency_error", run.record["results"][-1])
                (library / "Entropy.cpp").write_bytes(b"a different implementation")
                run.compile("fake-cli", sketch, (), output)
                self.assertIn("Entropy", run.record["results"][-1]["dependency_error"])
            self.assertFalse(run.finish())

    def test_ignored_security_flags_in_actual_commands_fail_build(self):
        with tempfile.TemporaryDirectory(prefix="protogen-security-flags-") as tmp:
            root = Path(tmp)
            self.make_project(root)
            run = BuildRun(root, "build")
            sketch = run.snapshot()
            run.record["toolchain"] = {"libraries": {}, "bundled_libraries": {}}
            output = root / "output"
            (output / "objects").mkdir(parents=True)
            (output / "objects/compile_commands.json").write_text(json.dumps([
                {"arguments": ["c++", "-Werror"]}]))
            private = root / "private"
            subprocess.run([os.sys.executable, str(ROOT / "scripts/provision-uart-auth.py"),
                            "--output", str(private)], check=True, capture_output=True)
            with patch("subprocess.run", return_value=subprocess.CompletedProcess(
                    [], 0, "build.flags.defs=-D__IMXRT1062__ -DTEENSYDUINO=160\n", "")):
                run.compile("fake-cli", sketch, ("PROTOGEN_UART_AUTH",), output,
                            extra_flags="-DPROTOGEN_UART_AUTH=1 -I" + str(private) + " -Werror")
            self.assertIn("security flags", run.record["results"][-1]["dependency_error"])
            self.assertFalse(run.finish())


class SettingsChecks(unittest.TestCase):
    def test_invalid_settings_rejected_before_narrowing(self):
        source = (ROOT / "firmware/PROT_OS7_MAX7219/Settings.h").read_text()
        mutations = [("Brightness", "256"), ("Brightness", "-1"),
                     ("StartupExpression", "256"), ("StartupExpression", "-1"),
                     ("BoopThreshold", "550"), ("MicrophoneSensitivity", "1024"),
                     ("ButtonHoldMs", "10")]
        intervals = re.findall(r"constexpr uint32_t (\w+Ms) =", source)
        mutations += [(name, value) for name in intervals for value in ("0", "0xffffffffUL") if not (value == "0" and name in ("TransitionMs", "BlinkJitterMs"))]
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
