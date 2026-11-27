#!/usr/bin/env python3
"""Verify that version-matched dependency mutations cannot pass content checks."""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_support import _inspect_libraries, dependency_tree_digest, verify_compiler_tools


class DependencyIntegrity(unittest.TestCase):
    def test_mutated_selected_library(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            lib = root / "library"
            lib.mkdir()
            (lib / "library.properties").write_text("name=arduinoFFT\nversion=2.0.4\n")
            src = lib / "src"
            src.mkdir()
            source = src / "test.cpp"
            source.write_text("// trusted")
            lock = {
                "libraries": {"arduinoFFT": "2.0.4"},
                "bundled_libraries": {},
                "source_integrity": {
                    "libraries": {"arduinoFFT": {"tree_sha256": dependency_tree_digest(lib)}}
                },
            }
            (root / "libraries.cache").write_text(json.dumps([str(src)]))
            self.assertEqual(_inspect_libraries(root, lock, ["Microphone"])[1], [])
            source.write_text("// altered")
            self.assertEqual(_inspect_libraries(root, lock, ["Microphone"])[1], ["arduinoFFT"])

    def test_compiler_tree_tampering_and_host_scope(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            tool = root / "packages/teensy/tools/compiler/1"
            tool.mkdir(parents=True)
            executable = tool / "compiler"
            executable.write_bytes(b"original")
            pins = {"compiler": {"version": "1", "linux_x86_64_tree_sha256": dependency_tree_digest(tool)}}
            with patch("build_support.platform.system", return_value="Linux"), patch(
                "build_support.platform.machine", return_value="x86_64"
            ):
                self.assertEqual(verify_compiler_tools(root, "teensy", pins)["state"], "verified")
                executable.write_bytes(b"modified")
                with self.assertRaisesRegex(RuntimeError, "compiler tool content mismatch"):
                    verify_compiler_tools(root, "teensy", pins)
            with patch("build_support.platform.system", return_value="Darwin"):
                self.assertEqual(verify_compiler_tools(root, "teensy", pins)["state"], "unverified_host")

    def test_tree_identity_covers_names_and_contents(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            first = root / "first"
            first.write_text("data")
            original = dependency_tree_digest(root, platform_metadata=True)
            (root / "installed.json").write_text("metadata")
            self.assertEqual(original, dependency_tree_digest(root, platform_metadata=True))
            first.rename(root / "second")
            self.assertNotEqual(original, dependency_tree_digest(root, platform_metadata=True))
            (root / "link").symlink_to(root / "second")
            with self.assertRaises(RuntimeError):
                dependency_tree_digest(root)


if __name__ == "__main__":
    unittest.main()
