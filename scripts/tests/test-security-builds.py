#!/usr/bin/env python3
"""Exercise unsafe firmware keys and v4 isolation without provisioning hardware."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import runpy
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from build_support import BuildRun
from uart_key import compiler_options, validate_uart_key_directory, validate_uart_key_flags

spec = importlib.util.spec_from_file_location("check_v4", ROOT / "scripts/check-v4.py")
v4 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(v4)
KEY = hashlib.sha256(b"PUBLIC synthetic build-validation control; never provision").digest()


def header(values):
    return "#pragma once\n#include <stdint.h>\nstatic const uint8_t PROTOGEN_UART_AUTH_KEY[32] = {" + values + "};\n"


def write_key(directory, key=KEY):
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "ProtogenUartKey.h").write_text(header(",".join(str(value) for value in key)))


class KeyChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="protogen-key-check-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.private = self.root / "private key"
        write_key(self.private)

    def test_generated_key_and_symlink_alias_work_without_disclosing_key(self):
        generated = self.root / "generated"
        result = subprocess.run([sys.executable, str(ROOT / "scripts/provision-uart-auth.py"),
                                 "--output", str(generated)], capture_output=True, text=True, check=True)
        self.assertEqual(validate_uart_key_directory(generated), generated.resolve())
        self.assertNotIn("PROTOGEN_UART_AUTH_KEY[", result.stdout + result.stderr)
        alias = self.root / "alias"
        alias.symlink_to(generated, target_is_directory=True)
        self.assertEqual(validate_uart_key_directory(alias), generated.resolve())

    def test_hex_comments_and_trailing_comma_match_compiler(self):
        text = header(", /* byte */ ".join(f"0x{value:02x}U" for value in KEY) + ",")
        (self.private / "ProtogenUartKey.h").write_text(text)
        validate_uart_key_directory(self.private)
        result = subprocess.run([os.getenv("CXX", "clang++"), "-std=c++17", "-x", "c++",
                                 "-fsyntax-only", "-I", str(self.private), "-"],
                                input="#include <ProtogenUartKey.h>\n", capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, "Accepted header failed compiler syntax check")

    def test_missing_key_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "Missing"):
            validate_uart_key_directory(self.root / "absent")

    def test_public_fixture_and_copied_expanded_fixture_are_rejected(self):
        with self.assertRaises(RuntimeError):
            validate_uart_key_directory(ROOT / "scripts/tests/uart-fixture")
        for text in ((ROOT / "scripts/tests/uart-fixture/ProtogenUartKey.h").read_text(),
                     header(",".join(map(str, [13] + [0] * 31)))):
            (self.private / "ProtogenUartKey.h").write_text(text)
            with self.assertRaises(RuntimeError):
                validate_uart_key_directory(self.private)

    def test_malformed_or_preprocessed_keys_fail_without_echoing_contents(self):
        valid = header(",".join(map(str, KEY)))
        forms = [header("13"), header(",".join(map(str, KEY)) + ",,"), header(",".join(map(str, KEY[:-1]))),
                 header(",".join(map(str, KEY + b"\x01"))),
                 valid.replace("[32]", "[33]"), valid.replace("[32]", "[]"),
                 valid.replace(str(KEY[0]) + ",", "256,", 1),
                 valid.replace(str(KEY[0]) + ",", "-1,", 1),
                 valid.replace(str(KEY[0]) + ",", "010,", 1),
                 valid.replace(str(KEY[0]) + ",", "(1<<5),", 1),
                 "#define SECRET_MARKER 1\n" + valid,
                 "#if 0\n" + valid + "#endif\n", valid + valid,
                 valid + "#include \"replacement.h\"\n"]
        for text in forms:
            with self.subTest(form=forms.index(text)):
                (self.private / "ProtogenUartKey.h").write_text(text)
                with self.assertRaises(RuntimeError) as error:
                    validate_uart_key_directory(self.private)
                self.assertNotIn("SECRET_MARKER", str(error.exception))
                self.assertNotIn(",".join(map(str, KEY)), str(error.exception))

    def test_obviously_weak_full_length_keys_are_rejected(self):
        for key in (bytes(32), bytes([255] * 32), bytes([13] + [0] * 31),
                    b"abcd" * 8, bytes(range(32)), bytes(range(32, 0, -1)),
                    bytes(16) + bytes([1] * 16), bytes(24) + bytes(range(1, 9))):
            write_key(self.private, key)
            with self.assertRaisesRegex(RuntimeError, "weak"):
                validate_uart_key_directory(self.private)

    def test_comment_line_splicing_cannot_hide_implicitly_zero_filled_key(self):
        # The compiler removes the 31 apparent bytes as part of the continued
        # comment, leaving exactly the public {13} fixture. Validation must fail.
        values = ",".join(map(str, KEY[1:]))
        for splice in ("\\", "??/"):
            text = header("13,\n// Continued comment " + splice + "\n" + values + "\n")
            (self.private / "ProtogenUartKey.h").write_text(text)
            with self.assertRaisesRegex(RuntimeError, "continuations"):
                validate_uart_key_directory(self.private)
        (self.private / "ProtogenUartKey.h").write_text(header(
            "13,\n// Continued comment \\\n" + values + "\n"))
        program = '#include <ProtogenUartKey.h>\nint main() { for (int i=1;i<32;++i) if(PROTOGEN_UART_AUTH_KEY[i]) return 1; return PROTOGEN_UART_AUTH_KEY[0]!=13; }\n'
        executable = self.root / "spliced-key"
        subprocess.run([os.getenv("CXX", "clang++"), "-std=c++17", "-x", "c++", "-I", str(self.private),
                        "-o", str(executable), "-"], input=program, capture_output=True, text=True, check=True)
        self.assertEqual(subprocess.run([str(executable)]).returncode, 0)

    def test_include_paths_support_spaces_and_reject_shadowing(self):
        for flags in ("-I" + shlex.quote(str(self.private)), "-I " + shlex.quote(str(self.private)),
                      "-isystem " + shlex.quote(str(self.private))):
            self.assertEqual(validate_uart_key_flags(flags), self.private.resolve())
        shadow = self.root / "sketch"
        write_key(shadow, bytes([13] + [0] * 31))
        with self.assertRaises(RuntimeError):
            validate_uart_key_flags("-I" + shlex.quote(str(self.private)), [shadow])
        write_key(shadow)
        with self.assertRaisesRegex(RuntimeError, "Ambiguous"):
            validate_uart_key_flags("-I" + shlex.quote(str(self.private)), [shadow])

    def test_macro_presence_matches_ifdef(self):
        for flags in ("-DPROTOGEN_UART_AUTH", "-DPROTOGEN_UART_AUTH=0", "-D PROTOGEN_UART_AUTH=1"):
            self.assertIn("PROTOGEN_UART_AUTH", compiler_options(flags)[0])
        self.assertNotIn("PROTOGEN_UART_AUTH", compiler_options("-DPROTOGEN_UART_AUTH -UPROTOGEN_UART_AUTH")[0])

    def hook(self, directory=None, shadow=False, flags="", includes=(), definitions=(), src_flags=""):
        project = self.root / "esp"
        (project / "src").mkdir(parents=True, exist_ok=True)
        if shadow:
            write_key(project / "src", bytes([13] + [0] * 31))
        class Environment:
            additions = []
            def subst(self, value):
                return str(project) if value == "$PROJECT_DIR" else value
            def GetProjectOption(self, name, default=None):
                return {"build_flags": flags, "build_src_flags": src_flags}.get(name, default)
            def get(self, name, default=None):
                return {"CPPPATH": includes, "CPPDEFINES": definitions}.get(name, default)
            def Append(self, **values):
                self.additions.append(values)
        env = Environment()
        with patch.dict(os.environ, {}, clear=True):
            if directory:
                os.environ["PROTOGEN_UART_KEY_DIR"] = str(directory)
            runpy.run_path(str(ROOT / "scripts/esp32-uart-build.py"),
                           init_globals={"Import": lambda name: None, "env": env})
        return env.additions

    def test_direct_platformio_hook_default_and_private_key(self):
        self.assertEqual(self.hook(), [])
        self.assertEqual(self.hook(self.private)[0]["CPPDEFINES"], [("PROTOGEN_UART_AUTH", 1)])

    def test_direct_platformio_hook_rejects_fixture_and_shadow(self):
        with self.assertRaises(RuntimeError):
            self.hook(ROOT / "scripts/tests/uart-fixture")
        with self.assertRaises(RuntimeError):
            self.hook(self.private, shadow=True)

    def test_platformio_existing_includes_and_explicit_auth_cannot_bypass_checks(self):
        fixture = ROOT / "scripts/tests/uart-fixture"
        with self.assertRaises(RuntimeError):
            self.hook(self.private, includes=[str(fixture)])
        for flags in ("-DPROTOGEN_UART_AUTH", "-D PROTOGEN_UART_AUTH=0"):
            with self.assertRaises(RuntimeError):
                self.hook(flags=flags + " -I" + shlex.quote(str(fixture)))
        with self.assertRaises(RuntimeError):
            self.hook(includes=[str(fixture)], definitions=[("PROTOGEN_UART_AUTH", 1)])
        with self.assertRaises(RuntimeError):
            self.hook(src_flags="-DPROTOGEN_UART_AUTH=1 -I" + shlex.quote(str(fixture)))
        self.assertEqual(self.hook(flags="-DPROTOGEN_UART_AUTH -I" + shlex.quote(str(self.private))), [])

    def test_environment_include_shadow_is_rejected(self):
        fixture = ROOT / "scripts/tests/uart-fixture"
        for name in ("CPATH", "CPLUS_INCLUDE_PATH"):
            with patch.dict(os.environ, {name: str(fixture)}):
                with self.assertRaises(RuntimeError):
                    validate_uart_key_flags("-I" + shlex.quote(str(self.private)))

    def build_run(self):
        sketch = self.root / "sketch"
        sketch.mkdir(exist_ok=True)
        (sketch / "PROT_OS7_MAX7219.ino").write_text("// synthetic sketch\n")
        build = BuildRun(self.root, "key-check")
        build.record["toolchain"] = {"libraries": {}, "bundled_libraries": {}}
        output = self.root / "output"
        (output / "objects").mkdir(parents=True, exist_ok=True)
        return build, sketch, output

    def test_teensy_rejects_unsafe_key_for_feature_flag_and_board_macro(self):
        for features, definitions, flags in ((["PROTOGEN_UART_AUTH"], "-DTEENSYDUINO=160", "-DPROTOGEN_UART_AUTH=1"),
                                               ([], "-DTEENSYDUINO=160", "-D PROTOGEN_UART_AUTH=0"),
                                               ([], "-DPROTOGEN_UART_AUTH", "")):
            build, sketch, output = self.build_run()
            write_key(self.private, bytes([13] + [0] * 31))
            with patch("subprocess.run", return_value=subprocess.CompletedProcess(
                    [], 0, "build.flags.defs=" + definitions + "\n", "")) as compiler:
                with self.assertRaises(RuntimeError):
                    build.compile("fake-cli", sketch, features, output,
                                  extra_flags=flags + " -I" + shlex.quote(str(self.private)) + " -Werror")
                self.assertEqual(compiler.call_count, 1)  # Properties only, never compilation.

    def test_teensy_accepts_private_key_and_checks_actual_selected_includes(self):
        build, sketch, output = self.build_run()
        flags = "-DPROTOGEN_UART_AUTH=1 -I" + shlex.quote(str(self.private)) + " -Werror"
        arguments = ["c++", "-Werror", "-DPROTOGEN_UART_AUTH=1", "-I", str(self.private)]
        (output / "objects/compile_commands.json").write_text(json.dumps([{"arguments": arguments}]))
        with patch("subprocess.run", return_value=subprocess.CompletedProcess(
                [], 0, "build.flags.defs=-DTEENSYDUINO=160\n", "")), patch("builtins.print"):
            build.compile("fake-cli", sketch, [], output, extra_flags=flags)
            self.assertNotIn("dependency_error", build.record["results"][-1])
            arguments[-1] = str(ROOT / "scripts/tests/uart-fixture")
            (output / "objects/compile_commands.json").write_text(json.dumps([{"arguments": arguments}]))
            build.compile("fake-cli", sketch, [], output, extra_flags=flags)
            self.assertIn("dependency_error", build.record["results"][-1])

    def test_esp_wrapper_rejects_fixture_before_invoking_platformio(self):
        root = self.root / "wrapper"
        (root / "scripts").mkdir(parents=True)
        shutil.copy2(ROOT / "scripts/build-esp32.py", root / "scripts/build-esp32.py")
        shutil.copy2(ROOT / "toolchain.lock.json", root / "toolchain.lock.json")
        result = subprocess.run([sys.executable, str(root / "scripts/build-esp32.py")],
                                env=dict(os.environ, PYTHONPATH=str(ROOT / "scripts"),
                                         PIO="/missing/compiler", PROTOGEN_UART_KEY_DIR=str(ROOT / "scripts/tests/uart-fixture")),
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        report = json.loads((root / "build/esp32-build.json").read_text())
        self.assertIn("32 explicit", report["error"])
        self.assertNotIn("exit_code", report)


class V4IsolationChecks(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="protogen-v4-isolation-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for directory in ("firmware/esp32/src", "firmware/esp32/protocol_v4", "firmware/PROT_OS7_MAX7219", "build"):
            (self.root / directory).mkdir(parents=True)
        self.production = self.root / "firmware/esp32/src/main.cpp"
        self.production.write_text("// current maintained production firmware\n")
        self.cmake = self.production.with_name("CMakeLists.txt")
        shutil.copy2(ROOT / "firmware/esp32/src/CMakeLists.txt", self.cmake)
        (self.root / "toolchain.lock.json").write_text("{}")
        self.experimental = self.root / "firmware/esp32/protocol_v4/Worker.cpp"
        self.experimental.write_text("// experimental\n")
        self.addCleanup(patch.stopall)
        patch.object(v4, "ROOT", self.root).start()
        patch.object(v4, "V4", self.root / "firmware/esp32/protocol_v4").start()
        patch.object(v4, "B", self.root / "build").start()

    def test_current_production_is_valid_without_git_or_historical_baseline(self):
        before = v4.production_identity()
        self.assertIn("firmware/esp32/src/main.cpp", before["files"])
        self.assertNotIn("firmware/esp32/protocol_v4/Worker.cpp", before["files"])
        with patch("subprocess.run", side_effect=AssertionError("No git comparison should run")):
            v4.require_production_unchanged(before)
        self.experimental.write_text("// experimental change\n")
        v4.require_production_unchanged(before)

    def test_production_edit_addition_deletion_and_lock_change_fail(self):
        for mode in ("edit", "add", "delete", "lock"):
            with self.subTest(mode=mode):
                before = v4.production_identity()
                if mode == "edit": self.production.write_text("// changed\n")
                elif mode == "add": self.production.with_name("new.cpp").write_text("// new\n")
                elif mode == "delete": self.production.unlink()
                else: (self.root / "toolchain.lock.json").write_text('{"changed":true}')
                with self.assertRaisesRegex(RuntimeError, "changed during"):
                    v4.require_production_unchanged(before)

    def test_v4_production_reference_fails_before_compiler(self):
        self.cmake.write_text('idf_component_register(SRCS "../protocol_v4/Core.cpp")')
        with patch("subprocess.run") as compiler:
            with self.assertRaisesRegex(RuntimeError, "referenced"):
                v4.check_production_isolation()
            compiler.assert_not_called()

    def test_actual_platform_guards_still_reject_activation(self):
        with patch.object(v4, "ROOT", ROOT), patch.object(v4, "V4", ROOT / "firmware/esp32/protocol_v4"):
            self.assertEqual(v4.check_production_isolation(),
                             hashlib.sha256((ROOT / "firmware/esp32/src/CMakeLists.txt").read_bytes()).hexdigest())

    def test_missing_guard_or_compiler_failure_cannot_pass(self):
        for result in (subprocess.CompletedProcess([], 0, "", ""),
                       subprocess.CompletedProcess([], 1, "", "unrelated compiler error")):
            with patch("subprocess.run", return_value=result):
                with self.assertRaisesRegex(RuntimeError, "Activation guard failed"):
                    v4.check_production_isolation()


if __name__ == "__main__":
    unittest.main()
